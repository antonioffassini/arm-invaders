// arm_invaders_sim.c
// ARM Invaders — Simulador didático (versão turbinada: cores, HUD, animação, rand/save/load/script)
// Compile: gcc -std=c11 -O2 -Wall -Wextra -o arm_invaders_sim arm_invaders_sim.c
// Run:     ./arm_invaders_sim
//
// Melhorias:
// - HUD com cores ANSI, barras coloridas, flags destacadas, contador de turnos.
// - Aceita rX nos comandos (ex.: add r2 10 ou add 2 10).
// - "Naves" (uma por registrador) com movimento simples a cada comando (animação no terminal).
// - Novos comandos: rand x min max, save/load estado, script arquivo.
// - Explosões ASCII e efeitos visuais.
// - MOV aceita imediato geral: mov x k.
// - Troca de usleep() por nanosleep() (sem warnings).
//
// Observações didáticas:
// - Registradores de 32 bits (uint32_t).
// - Flags: ADD/SUB realistas; em MUL, C/V são didáticas.
//
// Comandos (case-insensitive):
//   add x k          -> r[x] = r[x] + k
//   sub x k          -> r[x] = r[x] - k
//   mul x y          -> r[x] = r[x] * r[y]
//   mov x k          -> r[x] = k
//   rand x a b       -> r[x] = aleatório [a,b]
//   save file.txt    -> salva estado
//   load file.txt    -> carrega estado
//   script file.txt  -> executa comandos do arquivo (ignora linhas vazias ou que começam com '#')
//   show/reset/help/quit/exit
//
// Autor: Você + assistente :)


#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#define _POSIX_C_SOURCE 199309L
#define NUM_REGS   8
#define BAR_WIDTH  28
#define FIELD_W    34   // largura do "campo" onde as naves se movem
#define ANIM_FRAMES 6

#define ANSI_RESET   "\x1b[0m"
#define ANSI_BOLD    "\x1b[1m"
#define ANSI_DIM     "\x1b[2m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_CLEAR   "\x1b[2J"
#define ANSI_HOME    "\x1b[H"

typedef struct {
    uint8_t N; // Negative
    uint8_t Z; // Zero
    uint8_t C; // Carry
    uint8_t V; // Overflow
} Flags;

typedef struct {
    uint32_t r[NUM_REGS];
    Flags flags;
    int pos[NUM_REGS];     // posição horizontal da "nave" (0..FIELD_W-1)
    int dir[NUM_REGS];     // direção da nave (-1/+1)
    uint64_t turns;        // contador de turnos
} CPU;

// ------------------------ Utils ------------------------ //
static uint32_t clamp_u32_to_100(uint32_t v) { return (v > 100u) ? 100u : v; }

static void sleep_us(unsigned int us) {
    struct timespec ts;
    ts.tv_sec  = us / 1000000u;
    ts.tv_nsec = (long)(us % 1000000u) * 1000L;
    nanosleep(&ts, NULL);
}

static uint32_t urand_range(uint32_t a, uint32_t b) {
    if (a > b) { uint32_t t = a; a = b; b = t; }
    uint64_t span = (uint64_t)b - (uint64_t)a + 1ull;
    // mistura duas chamadas para cobrir faixa grande
    uint64_t r = ((uint64_t)rand() << 32) ^ (uint64_t)rand();
    return a + (uint32_t)(r % span);
}

static int parse_reg(const char* tok, int* out) {
    if (!tok || !*tok) return 0;
    if (tok[0] == 'r' || tok[0] == 'R') tok++;
    char* end = NULL;
    long v = strtol(tok, &end, 10);
    if (end && *end == '\0' && v >= 0 && v < NUM_REGS) { *out = (int)v; return 1; }
    return 0;
}

static int parse_u32(const char* tok, uint32_t* out) {
    if (!tok) return 0;
    errno = 0;
    char* end = NULL;
    unsigned long v = strtoul(tok, &end, 10);
    if (errno) return 0;
    if (end && *end == '\0') { *out = (uint32_t)v; return 1; }
    return 0;
}

// ----------------- Flags helpers (ADD/SUB) --------------- //
static void update_flags_add(Flags* f, uint32_t a, uint32_t b, uint32_t res) {
    f->N = (res >> 31) & 1u;
    f->Z = (res == 0u) ? 1u : 0u;
    uint64_t wide = (uint64_t)a + (uint64_t)b;
    f->C = (wide > 0xFFFFFFFFULL) ? 1u : 0u;
    int32_t sa = (int32_t)a, sb = (int32_t)b, sr = (int32_t)res;
    f->V = ((~(sa ^ sb) & (sa ^ sr)) < 0) ? 1u : 0u;
}

static void update_flags_sub(Flags* f, uint32_t a, uint32_t b, uint32_t res) {
    f->N = (res >> 31) & 1u;
    f->Z = (res == 0u) ? 1u : 0u;
    f->C = (a >= b) ? 1u : 0u; // no-borrow
    int32_t sa = (int32_t)a, sb = (int32_t)b, sr = (int32_t)res;
    f->V = (((sa ^ sb) & (sa ^ sr)) < 0) ? 1u : 0u;
}

// MUL (didático): C=1 se produto > 32 bits; V=1 se produto estoura int32
static void update_flags_mul_didatic(Flags* f, uint32_t a, uint32_t b, uint32_t res) {
    f->N = (res >> 31) & 1u;
    f->Z = (res == 0u) ? 1u : 0u;
    uint64_t wide = (uint64_t)a * (uint64_t)b;
    f->C = (wide > 0xFFFFFFFFULL) ? 1u : 0u;
    int64_t s_wide = (int64_t)(int32_t)a * (int64_t)(int32_t)b;
    f->V = (s_wide < INT32_MIN || s_wide > INT32_MAX) ? 1u : 0u;
}

// ----------------- Operations ----------------- //
static void op_add_imm(CPU* cpu, int x, uint32_t k) {
    uint32_t a = cpu->r[x];
    uint32_t res = a + k;
    cpu->r[x] = res;
    update_flags_add(&cpu->flags, a, k, res);
    cpu->turns++;
    printf(ANSI_CYAN "[ASM] ADD r%d, r%d, #%" PRIu32 ANSI_RESET "\n", x, x, k);
}

static void op_sub_imm(CPU* cpu, int x, uint32_t k) {
    uint32_t a = cpu->r[x];
    uint32_t res = a - k;
    cpu->r[x] = res;
    update_flags_sub(&cpu->flags, a, k, res);
    cpu->turns++;
    printf(ANSI_CYAN "[ASM] SUB r%d, r%d, #%" PRIu32 ANSI_RESET "\n", x, x, k);
}

static void op_mul_reg(CPU* cpu, int x, int y) {
    uint32_t a = cpu->r[x], b = cpu->r[y];
    uint32_t res = a * b; // 32 bits baixos
    cpu->r[x] = res;
    update_flags_mul_didatic(&cpu->flags, a, b, res);
    cpu->turns++;
    printf(ANSI_CYAN "[ASM] MUL r%d, r%d, r%d" ANSI_RESET "\n", x, x, y);
}

static void op_mov_imm(CPU* cpu, int x, uint32_t k) {
    cpu->r[x] = k;
    cpu->flags.N = (k >> 31) & 1u;
    cpu->flags.Z = (k == 0u) ? 1u : 0u;
    cpu->flags.C = 0u; cpu->flags.V = 0u; // didático
    cpu->turns++;
    printf(ANSI_CYAN "[ASM] MOV r%d, #%" PRIu32 ANSI_RESET "\n", x, k);
}

// ----------------- Visuals ----------------- //
static void explosion_ascii(void) {
    printf(ANSI_RED
           "   _.-^^---....,,--       \n"
           "_--                  --_  \n"
           "<                        >\n"
           "|   BOOM! Register hit!  |\n"
           "\\._                  _./ \n"
           "   ```--. . , ; .--'''   \n"
           "         | |   |         \n"
           "       .-=||  | |=-.     \n"
           "       `-=#$%%&%%$#=-'     \n"
           "          | ;  :|         \n"
           " _____.,-#%%&$@%%#~,.____ \n" ANSI_RESET);
}

static void draw_bar(uint32_t value) {
    uint32_t v = clamp_u32_to_100(value);
    int filled = (int)((v * BAR_WIDTH) / 100u);
    const char* col = ANSI_GREEN;
    if (v <= 33) col = ANSI_RED;
    else if (v <= 66) col = ANSI_YELLOW;
    printf("%s", col);
    for (int i = 0; i < filled; ++i) putchar(0x2588);   // █
    printf(ANSI_DIM);
    for (int i = filled; i < BAR_WIDTH; ++i) putchar(0x2591); // ░
    printf(ANSI_RESET);
}

static void print_flags(const Flags* f) {
    printf("Flags: ");
    printf("N=%s%d%s ", f->N ? ANSI_RED : ANSI_DIM, f->N, ANSI_RESET);
    printf("Z=%s%d%s ", f->Z ? ANSI_GREEN : ANSI_DIM, f->Z, ANSI_RESET);
    printf("C=%s%d%s ", f->C ? ANSI_YELLOW : ANSI_DIM, f->C, ANSI_RESET);
    printf("V=%s%d%s", f->V ? ANSI_MAGENTA : ANSI_DIM, f->V, ANSI_RESET);
    putchar('\n');
}

static void draw_ship_line(int pos) {
    for (int i = 0; i < FIELD_W; ++i) {
        if (i == pos) { printf(ANSI_BOLD ANSI_BLUE "<>" ANSI_RESET); }
        else putchar(' ');
    }
}

static void show_state(const CPU* cpu) {
    printf(ANSI_CLEAR ANSI_HOME);
    printf(ANSI_BOLD "========================================\n");
    printf("          ARM Invaders (Sim)            \n");
    printf("========================================" ANSI_RESET "\n");
    printf("Turno: %" PRIu64 "\n\n", cpu->turns);

    for (int i = 0; i < NUM_REGS; ++i) {
        printf("r%d: %12" PRIu32 "  [", i, cpu->r[i]);
        draw_bar(cpu->r[i]);
        printf("]  ");
        draw_ship_line(cpu->pos[i]);
        if (cpu->r[i] == 0u) printf("  " ANSI_RED "*EXPLODED*" ANSI_RESET);
        putchar('\n');
    }
    print_flags(&cpu->flags);
    printf("----------------------------------------\n\n");

    puts("Comandos:");
    puts("  add x k        -> r[x] = r[x] + k            (ex: add r2 10)");
    puts("  sub x k        -> r[x] = r[x] - k            (ex: sub 2 5)");
    puts("  mul x y        -> r[x] = r[x] * r[y]         (ex: mul r3 r1)");
    puts("  mov x k        -> r[x] = k                   (ex: mov 7 0)");
    puts("  rand x a b     -> r[x] = aleatório [a,b]     (ex: rand r0 0 500)");
    puts("  save file.txt  -> salvar estado");
    puts("  load file.txt  -> carregar estado");
    puts("  script file    -> executar comandos do arquivo");
    puts("  show/reset/help/quit/exit");
}

static void anim_step(CPU* cpu) {
    for (int i = 0; i < NUM_REGS; ++i) {
        cpu->pos[i] += cpu->dir[i];
        if (cpu->pos[i] < 0) { cpu->pos[i] = 0; cpu->dir[i] = +1; }
        if (cpu->pos[i] >= FIELD_W) { cpu->pos[i] = FIELD_W-1; cpu->dir[i] = -1; }
    }
}

static void animate_and_redraw(CPU* cpu, int frames) {
    for (int f = 0; f < frames; ++f) {
        show_state(cpu);
        sleep_us(60000); // ~60 ms
        anim_step(cpu);
    }
    show_state(cpu); // frame final
}

// ----------------- State I/O ----------------- //
static int save_state(const CPU* cpu, const char* path) {
    FILE* fp = fopen(path, "w");
    if (!fp) return 0;
    fprintf(fp, "# ARM Invaders save\n");
    for (int i = 0; i < NUM_REGS; ++i)
        fprintf(fp, "r%d=%" PRIu32 "\n", i, cpu->r[i]);
    fprintf(fp, "N=%u Z=%u C=%u V=%u\n", cpu->flags.N, cpu->flags.Z, cpu->flags.C, cpu->flags.V);
    fclose(fp);
    return 1;
}

static int load_state(CPU* cpu, const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) return 0;
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char key[16]; char val[64];
        if (sscanf(line, "%15[^=]=%63s", key, val) == 2) {
            if (key[0] == 'r') {
                int idx = atoi(key + 1);
                if (idx >= 0 && idx < NUM_REGS) cpu->r[idx] = (uint32_t)strtoul(val, NULL, 10);
            } else if (strcmp(key,"N")==0) cpu->flags.N = (uint8_t)atoi(val);
            else if (strcmp(key,"Z")==0) cpu->flags.Z = (uint8_t)atoi(val);
            else if (strcmp(key,"C")==0) cpu->flags.C = (uint8_t)atoi(val);
            else if (strcmp(key,"V")==0) cpu->flags.V = (uint8_t)atoi(val);
        }
    }
    fclose(fp);
    return 1;
}

// ----------------- REPL + Parser ----------------- //
static void reset(CPU* cpu) {
    memset(cpu, 0, sizeof(*cpu));
    for (int i = 0; i < NUM_REGS; ++i) {
        cpu->r[i] = 100u;
        cpu->pos[i] = rand() % FIELD_W;
        cpu->dir[i] = (rand() & 1) ? +1 : -1;
        if (cpu->dir[i] == 0) cpu->dir[i] = +1;
    }
    cpu->flags.N = cpu->flags.Z = cpu->flags.C = cpu->flags.V = 0u;
    cpu->turns = 0;
}

static int process_line(CPU* cpu, const char* line); // fwd decl

static int run_script(CPU* cpu, const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) return 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        char* p = buf;
        while (isspace((unsigned char)*p)) ++p;
        if (*p == '\0' || *p == '#') continue;
        size_t n = strlen(p);
        if (n && p[n-1] == '\n') p[n-1] = '\0';
        process_line(cpu, p);
    }
    fclose(fp);
    return 1;
}

static int process_line(CPU* cpu, const char* line) {
    char tmp[256];
    strncpy(tmp, line, sizeof(tmp)-1);
    tmp[sizeof(tmp)-1] = '\0';
    for (char* t = tmp; *t; ++t) *t = (char)tolower((unsigned char)*t);

    char* tok = strtok(tmp, " \t");
    if (!tok) return 1;

    if (strcmp(tok, "add") == 0) {
        char* t1 = strtok(NULL, " \t"); char* t2 = strtok(NULL, " \t");
        int x; uint32_t k;
        if (parse_reg(t1, &x) && parse_u32(t2, &k)) { op_add_imm(cpu,x,k); animate_and_redraw(cpu, ANIM_FRAMES); if (cpu->r[x]==0u) explosion_ascii(); }
        else puts("Uso: add x k");
        return 1;
    }
    if (strcmp(tok, "sub") == 0) {
        char* t1 = strtok(NULL, " \t"); char* t2 = strtok(NULL, " \t");
        int x; uint32_t k;
        if (parse_reg(t1, &x) && parse_u32(t2, &k)) { op_sub_imm(cpu,x,k); animate_and_redraw(cpu, ANIM_FRAMES); if (cpu->r[x]==0u) explosion_ascii(); }
        else puts("Uso: sub x k");
        return 1;
    }
    if (strcmp(tok, "mul") == 0) {
        char* t1 = strtok(NULL, " \t"); char* t2 = strtok(NULL, " \t");
        int x,y;
        if (parse_reg(t1, &x) && parse_reg(t2, &y)) { op_mul_reg(cpu,x,y); animate_and_redraw(cpu, ANIM_FRAMES); if (cpu->r[x]==0u) explosion_ascii(); }
        else puts("Uso: mul x y");
        return 1;
    }
    if (strcmp(tok, "mov") == 0) {
        char* t1 = strtok(NULL, " \t"); char* t2 = strtok(NULL, " \t");
        int x; uint32_t k;
        if (parse_reg(t1, &x) && parse_u32(t2, &k)) { op_mov_imm(cpu,x,k); animate_and_redraw(cpu, ANIM_FRAMES); if (cpu->r[x]==0u) explosion_ascii(); }
        else puts("Uso: mov x k");
        return 1;
    }
    if (strcmp(tok, "rand") == 0) {
        char* t1 = strtok(NULL, " \t"); char* t2 = strtok(NULL, " \t"); char* t3 = strtok(NULL, " \t");
        int x; uint32_t a,b;
        if (parse_reg(t1, &x) && parse_u32(t2, &a) && parse_u32(t3, &b)) { uint32_t v=urand_range(a,b); op_mov_imm(cpu,x,v); animate_and_redraw(cpu, ANIM_FRAMES); if (cpu->r[x]==0u) explosion_ascii(); }
        else puts("Uso: rand x min max");
        return 1;
    }
    if (strcmp(tok, "save") == 0) {
        char* t1 = strtok(NULL, " \t");
        if (t1 && save_state(cpu,t1)) printf("Estado salvo em '%s'.\n", t1);
        else puts("Falha ao salvar. Uso: save arquivo.txt");
        return 1;
    }
    if (strcmp(tok, "load") == 0) {
        char* t1 = strtok(NULL, " \t");
        if (t1 && load_state(cpu,t1)) { printf("Estado carregado de '%s'.\n", t1); animate_and_redraw(cpu, 2); }
        else puts("Falha ao carregar. Uso: load arquivo.txt");
        return 1;
    }
    if (strcmp(tok, "script") == 0) {
        char* t1 = strtok(NULL, " \t");
        if (t1 && run_script(cpu,t1)) { animate_and_redraw(cpu, 2); }
        else puts("Falha ao executar script. Uso: script arquivo.txt");
        return 1;
    }
    if (strcmp(tok, "show") == 0) { show_state(cpu); return 1; }
    if (strcmp(tok, "reset") == 0) { reset(cpu); animate_and_redraw(cpu, 2); return 1; }
    if (strcmp(tok, "help") == 0) { show_state(cpu); return 1; }
    if (strcmp(tok, "quit") == 0 || strcmp(tok, "exit") == 0) return 0;

    puts("Comando não reconhecido. Digite 'help' para ajuda.");
    return 1;
}

int main(void) {
    srand((unsigned)time(NULL));
    CPU cpu;
    reset(&cpu);
    show_state(&cpu);

    char line[256];
    while (1) {
        printf("\n" ANSI_BOLD "> " ANSI_RESET);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) { puts("\nEOF recebido. Saindo."); break; }
        size_t n = strlen(line);
        if (n && line[n-1] == '\n') line[n-1] = '\0';
        if (line[0] == '\0') continue;

        int cont = process_line(&cpu, line);
        if (!cont) { puts("Saindo. Até mais!"); break; }
    }
    return 0;
}
