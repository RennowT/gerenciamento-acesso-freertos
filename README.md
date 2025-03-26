# Gerenciamento de Acesso com Freertos

Sistema para controle de acesso a salas usando Arduino, FreeRTOS e webserver.

## Funcionalidades
- Cadastro de usuários via serial
- Controle de portas com LEDs
- Armazenamento em Flash
- Webserver para controle remoto

---

### Hardware
- Placa ESP32 (com suporte a WiFi e FreeRTOS)

### Software
- PlatformIO Core ou PlatformIO IDE (VSCode)
- Dependências no `platformio.ini` (gerenciamento automático)

## Instalação

1. Clone o repositório:
   ```bash
   git clone https://github.com/RennowT/gerenciamento-acesso-freertos
   ```
2. Abra o projeto no VSCode com PlatformIO
3. Conecte a placa ESP32 via USB
4. Compile e faça upload:
    ```bash
    pio run --target upload
    ```