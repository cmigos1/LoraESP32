# 📡 LoRa Messenger ESP32 + TFT ST7789

> Monitor serial LoRa com interface gráfica, teclado T9, Bluetooth e criptografia AES para ESP32

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-orange.svg)](https://platformio.org/)
[![LVGL](https://img.shields.io/badge/LVGL-v9-blue.svg)](https://lvgl.io/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

> ⚡ **Gerenciamento Automático de Bibliotecas** - Todas as dependências são instaladas via PlatformIO. Veja [MIGRATION.md](MIGRATION.md) se está migrando de versões antigas.

## Índice

- [Sobre o Projeto](#sobre-o-projeto)
- [Funcionalidades](#funcionalidades)
- [Hardware Necessário](#hardware-necessário)
- [Pinout](#pinout)
- [Bibliotecas](#bibliotecas)
- [Instalação](#instalação)
- [Uso](#uso)
- [Estrutura do Código](#estrutura-do-código)
- [Segurança](#segurança)

---

## Sobre o Projeto

Sistema de comunicação LoRa com interface gráfica moderna em TFT, permitindo:
- **Envio de mensagens** usando teclado matricial T9 (estilo celular antigo)
- **Comunicação dual**: Teclado físico OU Bluetooth via app móvel
- **Criptografia AES-128** para mensagens privadas
- **Modo monitor** para escuta passiva
- **Monitoramento de bateria** em tempo real

---

## Funcionalidades

### Interface com 5 Telas

1. **Menu Principal**
   - Navegação por teclado numérico
   - Indicador de bateria em tempo real

2. **LoRa Messenger**
   - Teclado T9 para digitação (2=ABC, 3=DEF, etc.)
   - Envio com criptografia opcional
   - Log de mensagens enviadas/recebidas
   - Teclas especiais: [B]Voltar [C]Enviar [D]Apagar

3. **Monitor de Escuta**
   - Modo somente-leitura
   - Contador de mensagens
   - Decodificação automática de mensagens criptografadas

4. **Bluetooth**
   - BLE UART Service (Nordic UART)
   - Compatível com apps: **nRF Connect**, **Serial Bluetooth Terminal**
   - Mensagens via BT são retransmitidas pelo LoRa

5. **Bateria**
   - Tensão em tempo real
   - Barra de progresso visual
   - Percentual estimado (LiPo 3.0V-4.2V)

### Segurança

- **AES-128 ECB** com padding PKCS7
- Criptografia ativável/desativável (tecla 5 no menu)
- Chave customizável no código-fonte

### Multitarefa FreeRTOS

- **6 Tasks paralelas**: LVGL render, tick, teclado, LoRa, Bluetooth, bateria
- Interface responsiva sem travamentos
- Mutex para proteção de recursos compartilhados

---

## Hardware Necessário

| Componente | Especificação | Quantidade |
|------------|---------------|------------|
| **Microcontrolador** | ESP32 DevKit (30 pinos) | 1 |
| **Display** | TFT ST7789 240x280 SPI | 1 |
| **Módulo LoRa** | E32/DX-LR02 (UART) | 1 |
| **Teclado** | Matricial 4x4 membrana | 1 |
| **Bateria** | LiPo 3.7V (opcional) | 1 |
| **Resistores** | 100kΩ (divisor tensão) | 2 |
| **Protoboard/PCB** | Para montagem | 1 |

### Módulos Opcionais
- Antena LoRa 433MHz/915MHz (conforme região)
- Case impresso em 3D
- Regulador de tensão 3.3V (se usar bateria)

---

## Pinout

### Display TFT ST7789 (SPI)
```
TFT Pin    →  ESP32 GPIO
────────────────────────
MOSI (SDA) →  23
SCLK (SCL) →  18
CS         →  5
DC         →  2
RST        →  4
VCC        →  3.3V
GND        →  GND
BL (LED)   →  3.3V (ou PWM para controle)
```

### Módulo LoRa E32/DX-LR02 (UART)
```
LoRa Pin   →  ESP32 GPIO
────────────────────────
M0         →  21
M1         →  22
RXD        →  16 (TX do ESP32)
TXD        →  17 (RX do ESP32)
AUX        →  19
VCC        →  3.3V
GND        →  GND
```

### Teclado Matricial 4x4
```
Teclado    →  ESP32 GPIO
────────────────────────
ROW1       →  32 (OUTPUT)
ROW2       →  33 (OUTPUT)
ROW3       →  25 (OUTPUT)
ROW4       →  26 (OUTPUT)
COL1       →  27 (INPUT_PULLUP)
COL2       →  14 (INPUT_PULLUP)
COL3       →  12 (INPUT_PULLUP)
COL4       →  13 (INPUT_PULLUP)
```

**Layout do Teclado:**
```
┌─────┬─────┬─────┬─────┐
│  1  │2ABC │3DEF │  A  │  [A] Menu
├─────┼─────┼─────┼─────┤
│4GHI │5JKL │6MNO │  B  │  [B] Voltar
├─────┼─────┼─────┼─────┤
│7PQRS│8TUV │9WXYZ│  C  │  [C] Enviar
├─────┼─────┼─────┼─────┤
│  *  │0 _  │  #  │  D  │  [D] Apagar
└─────┴─────┴─────┴─────┘
```

## Bibliotecas

### Dependências PlatformIO (`platformio.ini`)

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

lib_deps = 
    bodmer/TFT_eSPI @ ^2.5.43
    lvgl/lvgl @ ^9.0.0
    h2zero/NimBLE-Arduino @ ^1.4.1
    rweather/Crypto @ ^0.4.0

build_flags = 
    -DUSER_SETUP_LOADED=1
    -DST7789_DRIVER=1
    -DTFT_WIDTH=240
    -DTFT_HEIGHT=280
    -DTFT_MOSI=23
    -DTFT_SCLK=18
    -DTFT_CS=5
    -DTFT_DC=2
    -DTFT_RST=4
    -DLOAD_GLCD=1
    -DLOAD_FONT2=1
    -DLOAD_FONT4=1
    -DLOAD_FONT6=1
    -DLOAD_FONT7=1
    -DLOAD_FONT8=1
    -DLOAD_GFXFF=1
    -DSMOOTH_FONT=1
    -DSPI_FREQUENCY=40000000
```

### Bibliotecas Utilizadas

- **TFT_eSPI**: Driver para display ST7789
- **LVGL**: Framework de UI gráfica embarcada
- **NimBLE**: Stack Bluetooth Low Energy otimizado
- **Crypto**: Biblioteca AES/criptografia

---

## Instalação

### 1. Pré-requisitos

- [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) (plugin do VS Code)
- [Python 3.7+](https://www.python.org/downloads/)
- [Git](https://git-scm.com/downloads)

### 2. Clone o Repositório

```bash
git clone https://github.com/seu-usuario/TFT_Lora.git
cd TFT_Lora
```

### 3. Instale as Dependências

O PlatformIO baixará automaticamente todas as bibliotecas ao compilar:

```bash
# As bibliotecas serão baixadas automaticamente do platformio.ini
pio lib install
```

**Bibliotecas instaladas automaticamente:**
- `bodmer/TFT_eSPI@^2.5.43` - Driver display ST7789
- `lvgl/lvgl@^9.4.0` - Framework UI gráfica
- `h2zero/NimBLE-Arduino@^1.4.0` - Stack Bluetooth LE
- `rweather/Crypto@^0.4.0` - Criptografia AES

### 4. Configure o Hardware

1. **Conecte o display TFT** conforme o [pinout](#pinout)
2. **Conecte o módulo LoRa** nas portas UART
3. **Monte o teclado matricial** com os GPIOs especificados
4. **(Opcional)** Conecte a bateria com divisor resistivo

### 5. Ajuste a Chave AES (IMPORTANTE!)

No arquivo `src/main.cpp`, linha ~80, **troque a chave padrão**:

```cpp
const uint8_t AES_KEY[16] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
};
```

 **Use uma chave aleatória em produção!**

### 6. Compile e Upload

```bash
# Via PlatformIO CLI
pio run --target upload

# Ou use o botão "Upload" no VS Code (PlatformIO)
```

### 7. Monitor Serial (Opcional)

```bash
pio device monitor -b 115200
```

---

## Uso

### Navegação

1. **Menu Principal**: Use teclas **1-5** para selecionar opções
2. **Voltar**: Pressione **[B]** em qualquer tela
3. **Toggle Criptografia**: Tecla **5** no menu principal

### Enviar Mensagens (Teclado T9)

1. Acesse **LoRa Messenger** (tecla 1)
2. Digite usando teclado T9:
   - `2` = A → B → C → 2
   - `7` = P → Q → R → S → 7
   - `0` = Espaço → 0
3. Pressione **[C]** para enviar
4. Use **[D]** para apagar último caractere

**Exemplo:**
```
Para digitar "HELLO":
4(G)4(H) + 3(D)3(E) + 5(J)5(K)5(L) + 5(J)5(K)5(L) + 6(M)6(N)6(O)
```

### Conectar via Bluetooth

1. Acesse **Bluetooth** (tecla 3)
2. No smartphone, baixe **nRF Connect** ou **Serial Bluetooth Terminal**
3. Procure dispositivo **"ESP32_LoRa"**
4. Conecte ao serviço **Nordic UART**
5. Envie mensagens - serão retransmitidas via LoRa!

### Monitorar Mensagens

1. Acesse **Monitor** (tecla 2)
2. Dispositivo fica em modo escuta
3. Mensagens criptografadas são decodificadas automaticamente

---

## Estrutura do Código

```
src/main.cpp
├── Configuração de Pinos (linhas 40-60)
├── Mapeamento T9 (linhas 80-120)
├── Criptografia AES (linhas 150-250)
│   ├── encryptMessage()
│   └── decryptMessage()
├── Funções do Teclado (linhas 300-400)
│   ├── initKeypad()
│   ├── scanKeypad()
│   └── getT9Char()
├── Criação de UI (LVGL) (linhas 500-800)
│   ├── createMenuScreen()
│   ├── createLoRaScreen()
│   ├── createMonitorScreen()
│   ├── createBluetoothScreen()
│   └── createBatteryScreen()
├── Processamento de Teclas (linhas 850-1050)
│   ├── processMenuKey()
│   ├── processLoRaKey()
│   └── processBatteryKey()
└── FreeRTOS Tasks (linhas 1100-1400)
    ├── lvglTask (Core 1 - Renderização)
    ├── keypadTask (Core 0 - Entrada)
    ├── loraTask (Core 1 - UART RX/TX)
    ├── bluetoothTask (Core 1 - BLE)
    └── batteryTask (Core 0 - ADC)
```

### Tasks FreeRTOS

| Task | Core | Prioridade | Função |
|------|------|------------|--------|
| `lvglTask` | 1 | 2 | Renderização LVGL |
| `lvglTickTask` | 0 | 1 | Timer LVGL (1ms) |
| `keypadTask` | 0 | 3 | Scan teclado matricial |
| `loraTask` | 1 | 2 | RX/TX LoRa UART |
| `bluetoothTask` | 1 | 1 | BLE callbacks + messaging |
| `batteryTask` | 0 | 1 | Leitura ADC (2s) |

---

## Segurança

### Notas Importantes

1. **AES-128 ECB**: Modo simples, adequado para mensagens curtas. Para maior segurança, considere:
   - AES-CBC com IV randômico
   - AES-GCM para autenticação

2. **Chave Hardcoded**: Em produção, use:
   - EEPROM/Flash para armazenamento seguro
   - Key derivation functions (PBKDF2)
   - Hardware security modules (se disponível)

3. **LoRa é broadcasting**: Qualquer dispositivo com a mesma frequência pode interceptar. A criptografia é essencial para privacidade.

### Melhorias de Segurança (TODO)

- [ ] Implementar AES-CBC + IV
- [ ] Sistema de chaves por sessão
- [ ] Autenticação de mensagens (HMAC)
- [ ] Armazenamento seguro de chaves

---

## Personalização

### Alterar Cores da UI

No código, procure por `lv_color_hex()`:

```cpp
// Exemplo: Mudar cor do header
lv_obj_set_style_bg_color(header, lv_color_hex(0x1a1a2e), 0);
                                              // ^^^^^^^^ Altere aqui
```

**Cores Atuais:**
- Background: `0x0f0f23` (Azul escuro)
- Header: `0x1a1a2e` (Azul médio)
- Texto principal: `0xeaeaea` (Branco)
- Destaque: `0x00CCFF` (Ciano)

### Ajustar Timeout T9

```cpp
#define T9_TIMEOUT 1000  // ms para confirmar caractere (linha ~140)
```

### Configurar Módulo LoRa

Ajuste M0/M1 para diferentes modos de operação (consulte datasheet do seu módulo):

```cpp
// Modo Normal (0,0)
digitalWrite(LORA_M0, LOW);
digitalWrite(LORA_M1, LOW);

// Modo Wake-up (0,1)
// Modo Power Saving (1,0)
// Modo Sleep (1,1)
```

---

## Agradecimentos

- **LVGL** - Framework de UI embarcada
- **TFT_eSPI** - Driver versátil para displays TFT
- **NimBLE** - Stack Bluetooth otimizado
- Comunidade **ESP32** e **Arduino**

