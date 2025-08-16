ARM Invaders — Simulador Didático de Registradores ARM

ARM Invaders é um mini-jogo/simulador interativo desenvolvido para fins didáticos na disciplina Redes II. Ele permite visualizar e manipular os registradores r0..r7 e as flags N/Z/C/V em tempo real, enquanto comandos básicos (ADD, SUB, MUL, MOV) são executados.
A versão atual conta com melhorias como: cores ANSI, HUD, “naves” animadas, geração de valores aleatórios (rand), salvamento/carregamento de estado (save/load) e execução automatizada de comandos (script).

🎯 Objetivos e Motivação

O projeto busca:

Tornar o estudo de registradores ARM mais visual e interativo, ajudando a fixar conceitos de arquitetura de computadores e programação de baixo nível.

Demonstrar de forma prática como instruções simples modificam o estado interno de um processador.

Criar um recurso acessível e multiplataforma (roda no terminal, sem dependências pesadas).

Conectar os conteúdos vistos em Redes II com fundamentos de hardware, simulando a base sobre a qual sistemas e redes operam.

📚 Relação com a Disciplina

Embora a disciplina trate principalmente de protocolos e comunicação em rede, todo o tráfego e processamento em um sistema real passa pela CPU.
Este simulador ajuda a entender:

Como dados são manipulados internamente (base para a execução de protocolos e operações em rede).

A importância de instruções de baixo nível no desempenho e eficiência de sistemas conectados.

A relação entre hardware, sistema operacional e aplicações em rede.

🛠️ Decisões de Design   

Simplicidade intencional: focamos em operações básicas para manter a curva de aprendizado suave, permitindo que qualquer aluno possa entender e modificar o código.

Terminal puro: para facilitar a execução em qualquer ambiente (Linux, WSL, Raspberry Pi) e reforçar o uso de ferramentas de linha de comando.

Código comentado: cada função possui comentários que explicam sua função e lógica, incentivando a exploração e personalização.

Extensível: novos comandos podem ser facilmente adicionados ao interpretador.

📋 Funcionalidades

Visualização contínua do estado dos registradores e flags.

Execução de operações aritméticas (add, sub, mul, mov).

Geração de valores aleatórios (rand).

Salvamento e carregamento de estado (save / load).

Execução de scripts de comandos (script).

Interface visual simplificada com "vidas" representadas por barras.

📦 Estrutura do Repositório
arm_invaders/
│
├── src/                  # Código-fonte
├── doc/                  # Documentação em PDF
├── media/                # Imagens, vídeos e capturas de tela
├── scripts/              # Scripts de exemplo
├── LICENSE               # Licença do projeto
├── Makefile              # Script de compilação
└── README.md             # Este arquivo

▶️ Como Compilar e Executar

Requisitos: gcc (no Ubuntu: sudo apt install build-essential)

# Compilar
make

# Executar
make run

📄 Licença

Distribuído sob a licença MIT. Consulte o arquivo LICENSE para mais informaçõe
