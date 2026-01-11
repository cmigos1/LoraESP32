/*
 * Monitor Serial LoRa com Menu, Teclado T9, Bluetooth e Criptografia
 * TFT ST7789 240x280 + FreeRTOS Tasks + LVGL
 * 
 * === FUNCIONALIDADES ===
 * 1. Comunicação LoRa com teclado matricial T9
 * 2. Modo Bluetooth para mensagens via terminal
 * 3. Monitoramento de bateria (divisor de tensão)
 * 4. Criptografia AES para mensagens
 * 
 * === PINOUT ===
 * TFT ST7789:
 *   MOSI = 23, SCLK = 18, CS = 5, DC = 2, RST = 4
 * 
 * LoRa (DX-LR02 / E32 UART):
 *   M0 = 21, M1 = 22, RXD = 17, TXD = 16, AUX = 19
 * 
 * Teclado Matricial 4x4:
 *   Linhas (OUTPUT): 32, 33, 25, 26
 *   Colunas (INPUT_PULLUP): 27, 14, 12, 13
 * 
 * Bateria:
 *   ADC = GPIO 34 (divisor de tensão)
 * 
 * Layout Teclado T9:
 *   [1]    [2 ABC]  [3 DEF]   [A - Menu]
 *   [4 GHI][5 JKL]  [6 MNO]   [B - Voltar]
 *   [7 PQRS][8 TUV] [9 WXYZ]  [C - Enviar]
 *   [*]    [0 _]    [#]       [D - Apagar]
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <NimBLEDevice.h>
#include <AES.h>
#include <Crypto.h>

// ============================================
// CONFIGURAÇÃO DE PINOS
// ============================================

// Pinos LoRa UART
#define LORA_RX_PIN 16
#define LORA_TX_PIN 17
#define LORA_M0     21
#define LORA_M1     22
#define LORA_AUX    19

// Teclado Matricial 4x4
const uint8_t ROW_PINS[4] = {32, 33, 25, 26};  // Linhas (OUTPUT)
const uint8_t COL_PINS[4] = {27, 14, 12, 13};  // Colunas (INPUT_PULLUP)

// Bateria ADC
#define BATTERY_PIN 34
#define VREF 3.3
#define ADC_MAX 4095.0
// Divisor: R1 = 100k, R2 = 100k -> Fator = 2
#define VOLTAGE_DIVIDER_FACTOR 2.0

// Display
#define SCREEN_W  240
#define SCREEN_H  280

// ============================================
// CRIPTOGRAFIA AES
// ============================================
// Chave AES 128 bits (16 bytes) - TROQUE para produção!
const uint8_t AES_KEY[16] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
};

AES128 aes128;

// ============================================
// MAPEAMENTO T9
// ============================================
// Teclas: 1, 2, 3, 4, 5, 6, 7, 8, 9, *, 0, #, A, B, C, D
const char T9_MAP[16][5] = {
    {'1', '.', ',', '!', '?'},   // 1
    {'A', 'B', 'C', '2', '\0'},  // 2
    {'D', 'E', 'F', '3', '\0'},  // 3
    {'A', '\0', '\0', '\0', '\0'}, // A - Menu (especial)
    {'G', 'H', 'I', '4', '\0'},  // 4
    {'J', 'K', 'L', '5', '\0'},  // 5
    {'M', 'N', 'O', '6', '\0'},  // 6
    {'B', '\0', '\0', '\0', '\0'}, // B - Voltar (especial)
    {'P', 'Q', 'R', 'S', '7'},   // 7
    {'T', 'U', 'V', '8', '\0'},  // 8
    {'W', 'X', 'Y', 'Z', '9'},   // 9
    {'C', '\0', '\0', '\0', '\0'}, // C - Enviar (especial)
    {'*', '+', '-', '\0', '\0'}, // *
    {' ', '0', '\0', '\0', '\0'},// 0
    {'#', '@', '\0', '\0', '\0'},// #
    {'D', '\0', '\0', '\0', '\0'} // D - Apagar (especial)
};

// Índice do mapeamento por posição no teclado
const uint8_t KEY_INDEX[4][4] = {
    {0,  1,  2,  3},   // 1, 2, 3, A
    {4,  5,  6,  7},   // 4, 5, 6, B
    {8,  9, 10, 11},   // 7, 8, 9, C
    {12, 13, 14, 15}   // *, 0, #, D
};

// ============================================
// ESTRUTURAS E ENUMS
// ============================================

enum AppScreen {
    SCREEN_MENU = 0,
    SCREEN_LORA,
    SCREEN_MONITOR,
    SCREEN_BLUETOOTH,
    SCREEN_BATTERY,
    SCREEN_SETTINGS
};

enum KeypadState {
    KEY_NONE = 0,
    KEY_PRESSED,
    KEY_HELD,
    KEY_RELEASED
};

struct KeypadEvent {
    uint8_t row;
    uint8_t col;
    KeypadState state;
    uint32_t pressTime;
};

// ============================================
// VARIÁVEIS GLOBAIS
// ============================================

// Display e LVGL
TFT_eSPI tft = TFT_eSPI();
uint8_t *draw_buf = NULL;
#define BUF_SIZE (SCREEN_W * 30 * sizeof(lv_color_t))

// BLE (Nordic UART Service) - NimBLE
#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

NimBLEServer *pServer = NULL;
NimBLECharacteristic *pTxCharacteristic = NULL;
bool bleConnected = false;
bool bleInitialized = false;
String bleRxBuffer = "";
bool newBleData = false;

// Estado da aplicação
AppScreen currentScreen = SCREEN_MENU;
bool encryptionEnabled = true;

// Teclado T9
volatile KeypadEvent lastKeyEvent = {0, 0, KEY_NONE, 0};
uint8_t t9CharIndex = 0;
uint8_t lastKeyPressed = 255;
uint32_t lastKeyTime = 0;
#define T9_TIMEOUT 1000  // ms para confirmar caractere

// Buffer de mensagem
char messageBuffer[128] = "";
uint8_t messageLen = 0;

// Bateria
float batteryVoltage = 0.0;

// Mutex LVGL
SemaphoreHandle_t lvglMutex;

// Filas para comunicação entre tasks
QueueHandle_t keypadQueue;
QueueHandle_t messageQueue;

// ============================================
// OBJETOS LVGL UI
// ============================================

// Menu Principal
lv_obj_t *ui_menu_screen = NULL;
lv_obj_t *ui_menu_list = NULL;

// Tela LoRa
lv_obj_t *ui_lora_screen = NULL;

// Tela Monitor (somente escuta)
lv_obj_t *ui_monitor_screen = NULL;
lv_obj_t *ui_monitor_log = NULL;
lv_obj_t *ui_monitor_status = NULL;
uint32_t monitorMsgCount = 0;
lv_obj_t *ui_lora_log = NULL;
lv_obj_t *ui_lora_input = NULL;
lv_obj_t *ui_lora_status = NULL;

// Tela Bluetooth
lv_obj_t *ui_bt_screen = NULL;
lv_obj_t *ui_bt_log = NULL;
lv_obj_t *ui_bt_status = NULL;

// Tela Bateria
lv_obj_t *ui_battery_screen = NULL;
lv_obj_t *ui_battery_voltage = NULL;
lv_obj_t *ui_battery_bar = NULL;

// Header global
lv_obj_t *ui_header = NULL;
lv_obj_t *ui_header_title = NULL;
lv_obj_t *ui_header_battery = NULL;

// ============================================
// FUNÇÕES DE CRIPTOGRAFIA
// ============================================

// Encripta mensagem (padding PKCS7)
String encryptMessage(const String &plaintext) {
    uint8_t padLen = 16 - (plaintext.length() % 16);
    uint8_t inputLen = plaintext.length() + padLen;
    
    uint8_t *input = new uint8_t[inputLen];
    uint8_t *output = new uint8_t[inputLen];
    
    memcpy(input, plaintext.c_str(), plaintext.length());
    memset(input + plaintext.length(), padLen, padLen); // PKCS7 padding
    
    aes128.setKey(AES_KEY, 16);
    
    // Encripta em blocos de 16 bytes (ECB mode simples)
    for (int i = 0; i < inputLen; i += 16) {
        aes128.encryptBlock(output + i, input + i);
    }
    
    // Converte para hex string
    String hexOutput = "";
    for (int i = 0; i < inputLen; i++) {
        char hex[3];
        sprintf(hex, "%02X", output[i]);
        hexOutput += hex;
    }
    
    delete[] input;
    delete[] output;
    
    return hexOutput;
}

// Decripta mensagem
String decryptMessage(const String &cipherHex) {
    if (cipherHex.length() % 32 != 0) return "[ERRO DECRYPT]";
    
    uint8_t cipherLen = cipherHex.length() / 2;
    uint8_t *cipher = new uint8_t[cipherLen];
    uint8_t *output = new uint8_t[cipherLen];
    
    // Converte hex para bytes
    for (int i = 0; i < cipherLen; i++) {
        String byteStr = cipherHex.substring(i * 2, i * 2 + 2);
        cipher[i] = strtol(byteStr.c_str(), NULL, 16);
    }
    
    aes128.setKey(AES_KEY, 16);
    
    // Decripta em blocos de 16 bytes
    for (int i = 0; i < cipherLen; i += 16) {
        aes128.decryptBlock(output + i, cipher + i);
    }
    
    // Remove PKCS7 padding
    uint8_t padLen = output[cipherLen - 1];
    if (padLen > 16) padLen = 0;
    
    String plaintext = "";
    for (int i = 0; i < cipherLen - padLen; i++) {
        plaintext += (char)output[i];
    }
    
    delete[] cipher;
    delete[] output;
    
    return plaintext;
}

// ============================================
// FUNÇÕES DO TECLADO
// ============================================

void initKeypad() {
    for (int i = 0; i < 4; i++) {
        pinMode(ROW_PINS[i], OUTPUT);
        digitalWrite(ROW_PINS[i], HIGH);
        pinMode(COL_PINS[i], INPUT_PULLUP);
    }
}

int8_t scanKeypad() {
    for (int row = 0; row < 4; row++) {
        // Ativa linha (LOW)
        digitalWrite(ROW_PINS[row], LOW);
        delayMicroseconds(10);
        
        for (int col = 0; col < 4; col++) {
            if (digitalRead(COL_PINS[col]) == LOW) {
                digitalWrite(ROW_PINS[row], HIGH);
                return KEY_INDEX[row][col];
            }
        }
        
        digitalWrite(ROW_PINS[row], HIGH);
    }
    return -1; // Nenhuma tecla
}

char getT9Char(uint8_t keyIndex, uint8_t charIndex) {
    if (keyIndex >= 16) return '\0';
    
    uint8_t maxChars = 0;
    for (int i = 0; i < 5; i++) {
        if (T9_MAP[keyIndex][i] != '\0') maxChars++;
        else break;
    }
    
    if (maxChars == 0) return '\0';
    return T9_MAP[keyIndex][charIndex % maxChars];
}

bool isSpecialKey(uint8_t keyIndex) {
    // A=3, B=7, C=11, D=15
    return (keyIndex == 3 || keyIndex == 7 || keyIndex == 11 || keyIndex == 15);
}

// ============================================
// FUNÇÕES DA BATERIA
// ============================================

float readBatteryVoltage() {
    int raw = analogRead(BATTERY_PIN);
    float voltage = (raw / ADC_MAX) * VREF * VOLTAGE_DIVIDER_FACTOR;
    return voltage;
}

uint8_t voltageToPercent(float voltage) {
    // Aproximação linear simples (ajuste para sua bateria)
    // LiPo: 4.2V = 100%, 3.0V = 0%
    if (voltage >= 4.2) return 100;
    if (voltage <= 3.0) return 0;
    return (uint8_t)((voltage - 3.0) / 1.2 * 100);
}

// ============================================
// CALLBACKS LVGL
// ============================================

void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px, w * h, true);
    tft.endWrite();
    lv_display_flush_ready(disp);
}

// ============================================
// CRIAÇÃO DA UI - HEADER COMUM
// ============================================

lv_obj_t* createHeader(lv_obj_t *parent, const char *title) {
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, SCREEN_W, 45);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *lbl = lv_label_create(header);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x00CCFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);
    
    // Indicador de bateria
    lv_obj_t *batLbl = lv_label_create(header);
    lv_label_set_text(batLbl, "?.??V");
    lv_obj_set_style_text_color(batLbl, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(batLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(batLbl, LV_ALIGN_RIGHT_MID, -10, 0);
    
    ui_header_battery = batLbl;
    
    return header;
}

// ============================================
// CRIAÇÃO DA UI - MENU PRINCIPAL
// ============================================

void createMenuScreen() {
    ui_menu_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_menu_screen, lv_color_hex(0x0f0f23), 0);
    
    createHeader(ui_menu_screen, LV_SYMBOL_HOME " Menu");
    
    // Container do menu
    lv_obj_t *container = lv_obj_create(ui_menu_screen);
    lv_obj_set_size(container, SCREEN_W - 20, SCREEN_H - 60);
    lv_obj_align(container, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(container, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_radius(container, 10, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(container, 10, 0);
    lv_obj_set_style_pad_row(container, 8, 0);
    
    // Opções do menu
    const char *menuItems[] = {
        LV_SYMBOL_CALL " 1. LoRa Messenger",
        LV_SYMBOL_EYE_OPEN " 2. Monitor",
        LV_SYMBOL_BLUETOOTH " 3. Bluetooth",
        LV_SYMBOL_BATTERY_FULL " 4. Bateria",
        LV_SYMBOL_SETTINGS " 5. Crypto"
    };
    
    for (int i = 0; i < 5; i++) {
        lv_obj_t *btn = lv_btn_create(container);
        lv_obj_set_size(btn, SCREEN_W - 50, 38);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a1a40), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0f3460), LV_STATE_FOCUSED);
        lv_obj_set_style_radius(btn, 8, 0);
        
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, menuItems[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xeaeaea), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);
    }
    
    // Instrução
    lv_obj_t *hint = lv_label_create(ui_menu_screen);
    lv_label_set_text(hint, "[1-5] Selecionar");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
}

// ============================================
// CRIAÇÃO DA UI - TELA LORA
// ============================================

void createLoRaScreen() {
    ui_lora_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_lora_screen, lv_color_hex(0x0f0f23), 0);
    
    createHeader(ui_lora_screen, LV_SYMBOL_CALL " LoRa T9");
    
    // Área de log (mensagens recebidas/enviadas)
    ui_lora_log = lv_textarea_create(ui_lora_screen);
    lv_obj_set_size(ui_lora_log, SCREEN_W - 10, 130);
    lv_obj_align(ui_lora_log, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(ui_lora_log, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(ui_lora_log, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(ui_lora_log, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_color(ui_lora_log, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(ui_lora_log, 5, 0);
    lv_textarea_set_placeholder_text(ui_lora_log, "Mensagens...");
    lv_obj_remove_flag(ui_lora_log, LV_OBJ_FLAG_CLICKABLE);
    
    // Campo de entrada
    lv_obj_t *inputLabel = lv_label_create(ui_lora_screen);
    lv_label_set_text(inputLabel, "Mensagem:");
    lv_obj_set_style_text_color(inputLabel, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(inputLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(inputLabel, LV_ALIGN_TOP_LEFT, 10, 185);
    
    ui_lora_input = lv_textarea_create(ui_lora_screen);
    lv_obj_set_size(ui_lora_input, SCREEN_W - 10, 50);
    lv_obj_align(ui_lora_input, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_set_style_bg_color(ui_lora_input, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_text_color(ui_lora_input, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(ui_lora_input, &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_color(ui_lora_input, lv_color_hex(0x00CCFF), 0);
    lv_obj_set_style_radius(ui_lora_input, 5, 0);
    lv_textarea_set_placeholder_text(ui_lora_input, "Digite com T9...");
    
    // Status
    ui_lora_status = lv_label_create(ui_lora_screen);
    lv_label_set_text(ui_lora_status, "[B]Voltar [C]Enviar [D]Apagar");
    lv_obj_set_style_text_color(ui_lora_status, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(ui_lora_status, &lv_font_montserrat_10, 0);
    lv_obj_align(ui_lora_status, LV_ALIGN_BOTTOM_MID, 0, -5);
}

// ============================================
// CRIAÇÃO DA UI - TELA MONITOR (SOMENTE ESCUTA)
// ============================================

void createMonitorScreen() {
    ui_monitor_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_monitor_screen, lv_color_hex(0x0f0f23), 0);
    
    createHeader(ui_monitor_screen, LV_SYMBOL_EYE_OPEN " Monitor");
    
    // Status
    ui_monitor_status = lv_label_create(ui_monitor_screen);
    lv_label_set_text(ui_monitor_status, "Escutando... (0 msgs)");
    lv_obj_set_style_text_color(ui_monitor_status, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(ui_monitor_status, &lv_font_montserrat_12, 0);
    lv_obj_align(ui_monitor_status, LV_ALIGN_TOP_MID, 0, 50);
    
    // Área de log (mensagens recebidas)
    ui_monitor_log = lv_textarea_create(ui_monitor_screen);
    lv_obj_set_size(ui_monitor_log, SCREEN_W - 10, SCREEN_H - 100);
    lv_obj_align(ui_monitor_log, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_set_style_bg_color(ui_monitor_log, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(ui_monitor_log, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(ui_monitor_log, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_color(ui_monitor_log, lv_color_hex(0x444400), 0);
    lv_obj_set_style_radius(ui_monitor_log, 5, 0);
    lv_textarea_set_placeholder_text(ui_monitor_log, "Aguardando mensagens...");
    lv_obj_remove_flag(ui_monitor_log, LV_OBJ_FLAG_CLICKABLE);
    
    // Instrução
    lv_obj_t *hint = lv_label_create(ui_monitor_screen);
    lv_label_set_text(hint, "[B] Voltar | [D] Limpar");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
}

// ============================================
// CRIAÇÃO DA UI - TELA BLUETOOTH
// ============================================

void createBluetoothScreen() {
    ui_bt_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_bt_screen, lv_color_hex(0x0f0f23), 0);
    
    createHeader(ui_bt_screen, LV_SYMBOL_BLUETOOTH " Bluetooth");
    
    // Status BT
    ui_bt_status = lv_label_create(ui_bt_screen);
    lv_label_set_text(ui_bt_status, "BT: Desconectado");
    lv_obj_set_style_text_color(ui_bt_status, lv_color_hex(0xFF6600), 0);
    lv_obj_set_style_text_font(ui_bt_status, &lv_font_montserrat_14, 0);
    lv_obj_align(ui_bt_status, LV_ALIGN_TOP_MID, 0, 55);
    
    // Área de log
    ui_bt_log = lv_textarea_create(ui_bt_screen);
    lv_obj_set_size(ui_bt_log, SCREEN_W - 10, SCREEN_H - 110);
    lv_obj_align(ui_bt_log, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_set_style_bg_color(ui_bt_log, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(ui_bt_log, lv_color_hex(0x00CCFF), 0);
    lv_obj_set_style_text_font(ui_bt_log, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_color(ui_bt_log, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(ui_bt_log, 5, 0);
    lv_textarea_set_placeholder_text(ui_bt_log, "Use app 'nRF Connect' ou\n'Serial Bluetooth Terminal'\npara conectar via BLE");
    lv_obj_remove_flag(ui_bt_log, LV_OBJ_FLAG_CLICKABLE);
    
    // Instrução
    lv_obj_t *hint = lv_label_create(ui_bt_screen);
    lv_label_set_text(hint, "[B] Voltar | [C] Limpar | [D] Info");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
}

// ============================================
// CRIAÇÃO DA UI - TELA BATERIA
// ============================================

void createBatteryScreen() {
    ui_battery_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_battery_screen, lv_color_hex(0x0f0f23), 0);
    
    createHeader(ui_battery_screen, LV_SYMBOL_BATTERY_FULL " Bateria");
    
    // Ícone grande
    lv_obj_t *icon = lv_label_create(ui_battery_screen);
    lv_label_set_text(icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -60);
    
    // Tensão
    ui_battery_voltage = lv_label_create(ui_battery_screen);
    lv_label_set_text(ui_battery_voltage, "?.?? V");
    lv_obj_set_style_text_color(ui_battery_voltage, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_battery_voltage, &lv_font_montserrat_28, 0);
    lv_obj_align(ui_battery_voltage, LV_ALIGN_CENTER, 0, 0);
    
    // Barra de progresso
    ui_battery_bar = lv_bar_create(ui_battery_screen);
    lv_obj_set_size(ui_battery_bar, 180, 20);
    lv_bar_set_range(ui_battery_bar, 0, 100);
    lv_bar_set_value(ui_battery_bar, 50, LV_ANIM_ON);
    lv_obj_set_style_bg_color(ui_battery_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_battery_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui_battery_bar, 5, 0);
    lv_obj_align(ui_battery_bar, LV_ALIGN_CENTER, 0, 50);
    
    // Info
    lv_obj_t *info = lv_label_create(ui_battery_screen);
    lv_label_set_text(info, "Tensao direta do ADC\n(Divisor R1=R2=100k)");
    lv_obj_set_style_text_color(info, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 100);
    
    // Instrução
    lv_obj_t *hint = lv_label_create(ui_battery_screen);
    lv_label_set_text(hint, "[B] Voltar ao Menu");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
}

// ============================================
// NAVEGAÇÃO ENTRE TELAS
// ============================================

void switchScreen(AppScreen screen) {
    currentScreen = screen;
    
    switch (screen) {
        case SCREEN_MENU:
            lv_screen_load(ui_menu_screen);
            break;
        case SCREEN_LORA:
            lv_screen_load(ui_lora_screen);
            break;
        case SCREEN_MONITOR:
            lv_screen_load(ui_monitor_screen);
            break;
        case SCREEN_BLUETOOTH:
            // Atualiza status do BLE
            if (bleInitialized) {
                if (bleConnected) {
                    lv_label_set_text(ui_bt_status, "BLE Conectado!");
                    lv_obj_set_style_text_color(ui_bt_status, lv_color_hex(0x00FF00), 0);
                } else {
                    lv_label_set_text(ui_bt_status, "BLE: ESP32_LoRa (aguardando)");
                    lv_obj_set_style_text_color(ui_bt_status, lv_color_hex(0x00CCFF), 0);
                }
            } else {
                lv_label_set_text(ui_bt_status, "BLE: Inicializando...");
                lv_obj_set_style_text_color(ui_bt_status, lv_color_hex(0xFFAA00), 0);
            }
            lv_screen_load(ui_bt_screen);
            break;
        case SCREEN_BATTERY:
            lv_screen_load(ui_battery_screen);
            break;
        default:
            lv_screen_load(ui_menu_screen);
            break;
    }
}

// ============================================
// PROCESSAMENTO DE TECLAS
// ============================================

void processMenuKey(uint8_t keyIndex) {
    switch (keyIndex) {
        case 0: // Tecla 1 - LoRa Messenger
            switchScreen(SCREEN_LORA);
            break;
        case 1: // Tecla 2 - Monitor
            switchScreen(SCREEN_MONITOR);
            break;
        case 2: // Tecla 3 - Bluetooth
            switchScreen(SCREEN_BLUETOOTH);
            break;
        case 4: // Tecla 4 - Bateria
            switchScreen(SCREEN_BATTERY);
            break;
        case 5: // Tecla 5 - Toggle criptografia
            encryptionEnabled = !encryptionEnabled;
            Serial.printf("Criptografia: %s\n", encryptionEnabled ? "ON" : "OFF");
            break;
    }
}

void processMonitorKey(uint8_t keyIndex) {
    if (keyIndex == 7) { // B - Voltar
        switchScreen(SCREEN_MENU);
        return;
    }
    
    if (keyIndex == 15) { // D - Limpar log
        lv_textarea_set_text(ui_monitor_log, "");
        monitorMsgCount = 0;
        lv_label_set_text(ui_monitor_status, "Log limpo");
    }
}

void processLoRaKey(uint8_t keyIndex) {
    if (keyIndex == 7) { // B - Voltar
        // Confirma caractere pendente antes de sair
        if (messageLen > 0 && messageLen < 127) {
            // Caractere já está no buffer
        }
        messageBuffer[0] = '\0';
        messageLen = 0;
        lv_textarea_set_text(ui_lora_input, "");
        switchScreen(SCREEN_MENU);
        return;
    }
    
    if (keyIndex == 11) { // C - Enviar
        if (messageLen > 0) {
            String msg = String(messageBuffer);
            String toSend = msg;
            
            // Encripta se habilitado
            if (encryptionEnabled) {
                toSend = encryptMessage(msg);
                Serial.println("Msg encriptada: " + toSend);
            }
            
            // Envia via LoRa
            Serial2.println(toSend);
            
            // Adiciona ao log
            String logEntry = "> " + msg + "\n";
            lv_textarea_add_text(ui_lora_log, logEntry.c_str());
            
            // Limpa buffer
            messageBuffer[0] = '\0';
            messageLen = 0;
            lv_textarea_set_text(ui_lora_input, "");
        }
        return;
    }
    
    if (keyIndex == 15) { // D - Apagar
        if (messageLen > 0) {
            messageLen--;
            messageBuffer[messageLen] = '\0';
            lv_textarea_set_text(ui_lora_input, messageBuffer);
        }
        t9CharIndex = 0;
        lastKeyPressed = 255;
        return;
    }
    
    // Teclas numéricas T9
    if (!isSpecialKey(keyIndex)) {
        uint32_t now = millis();
        
        if (keyIndex == lastKeyPressed && (now - lastKeyTime) < T9_TIMEOUT) {
            // Mesma tecla - cicla caractere
            t9CharIndex++;
            if (messageLen > 0) {
                messageLen--; // Remove último para substituir
            }
        } else {
            // Nova tecla
            t9CharIndex = 0;
        }
        
        char c = getT9Char(keyIndex, t9CharIndex);
        if (c != '\0' && messageLen < 126) {
            messageBuffer[messageLen++] = c;
            messageBuffer[messageLen] = '\0';
            lv_textarea_set_text(ui_lora_input, messageBuffer);
        }
        
        lastKeyPressed = keyIndex;
        lastKeyTime = now;
    }
}

void processBluetoothKey(uint8_t keyIndex) {
    if (keyIndex == 7) { // B - Voltar
        switchScreen(SCREEN_MENU);
        return;
    }
    
    if (keyIndex == 11) { // C - Limpar log
        lv_textarea_set_text(ui_bt_log, "");
        lv_textarea_add_text(ui_bt_log, "Log limpo.\nAguardando mensagens...\n");
    }
    
    if (keyIndex == 15) { // D - Info
        String info = "Nome: ESP32_LoRa\n";
        info += "Tipo: BLE (UART)\n";
        info += bleConnected ? "Status: Conectado\n" : "Status: Aguardando...\n";
        info += "Use app: nRF Connect\n";
        lv_textarea_add_text(ui_bt_log, info.c_str());
    }
}

void processBatteryKey(uint8_t keyIndex) {
    if (keyIndex == 7) { // B - Voltar
        switchScreen(SCREEN_MENU);
    }
}

void handleKeyPress(uint8_t keyIndex) {
    if (xSemaphoreTake(lvglMutex, portMAX_DELAY)) {
        switch (currentScreen) {
            case SCREEN_MENU:
                processMenuKey(keyIndex);
                break;
            case SCREEN_LORA:
                processLoRaKey(keyIndex);
                break;
            case SCREEN_MONITOR:
                processMonitorKey(keyIndex);
                break;
            case SCREEN_BLUETOOTH:
                processBluetoothKey(keyIndex);
                break;
            case SCREEN_BATTERY:
                processBatteryKey(keyIndex);
                break;
            default:
                break;
        }
        xSemaphoreGive(lvglMutex);
    }
}

// ============================================
// TASKS FREERTOS
// ============================================

// Task LVGL (Renderização)
void lvglTask(void *pvParameters) {
    while (1) {
        if (xSemaphoreTake(lvglMutex, portMAX_DELAY)) {
            lv_timer_handler();
            xSemaphoreGive(lvglMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Task Tick LVGL
void lvglTickTask(void *pvParameters) {
    while (1) {
        lv_tick_inc(1);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Task Teclado
void keypadTask(void *pvParameters) {
    int8_t lastKey = -1;
    uint32_t debounceTime = 0;
    
    while (1) {
        int8_t key = scanKeypad();
        
        if (key != lastKey) {
            if (key >= 0 && (millis() - debounceTime) > 150) {
                Serial.printf("Tecla: %d\n", key);
                handleKeyPress(key);
                debounceTime = millis();
            }
            lastKey = key;
        }
        
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Task LoRa
void loraTask(void *pvParameters) {
    String incomingMsg = "";
    
    while (1) {
        if (Serial2.available()) {
            incomingMsg = Serial2.readStringUntil('\n');
            
            if (incomingMsg.length() > 0) {
                Serial.println("LoRa RX: " + incomingMsg);
                
                String displayMsg = incomingMsg;
                
                // Tenta decriptar se parece ser hex
                if (encryptionEnabled && incomingMsg.length() >= 32) {
                    bool isHex = true;
                    for (int i = 0; i < incomingMsg.length() && isHex; i++) {
                        char c = incomingMsg[i];
                        if (!isxdigit(c)) isHex = false;
                    }
                    
                    if (isHex) {
                        displayMsg = decryptMessage(incomingMsg);
                    }
                }
                
                if (xSemaphoreTake(lvglMutex, portMAX_DELAY)) {
                    String logEntry = "< " + displayMsg + "\n";
                    
                    // Atualiza tela LoRa
                    lv_textarea_add_text(ui_lora_log, logEntry.c_str());
                    
                    // Atualiza tela Monitor (sempre)
                    monitorMsgCount++;
                    lv_textarea_add_text(ui_monitor_log, logEntry.c_str());
                    
                    // Atualiza status do monitor
                    char statusStr[32];
                    sprintf(statusStr, "Escutando... (%lu msgs)", monitorMsgCount);
                    lv_label_set_text(ui_monitor_status, statusStr);
                    
                    xSemaphoreGive(lvglMutex);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================
// CALLBACKS BLE (NimBLE)
// ============================================

class MyServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        bleConnected = true;
        Serial.println("BLE: Cliente conectado");
    }

    void onDisconnect(NimBLEServer* pServer) {
        bleConnected = false;
        Serial.println("BLE: Cliente desconectado");
        // Reinicia advertising
        NimBLEDevice::startAdvertising();
    }
};

class MyCharacteristicCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            bleRxBuffer = String(rxValue.c_str());
            newBleData = true;
            Serial.println("BLE RX: " + bleRxBuffer);
        }
    }
};

// Task BLE (NimBLE)
void bluetoothTask(void *pvParameters) {
    // Aguarda sistema estabilizar
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    Serial.println("Iniciando NimBLE...");
    
    // Inicializa NimBLE
    NimBLEDevice::init("ESP32_LoRa");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max power
    
    // Cria servidor
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    
    // Cria serviço UART
    NimBLEService *pService = pServer->createService(SERVICE_UUID);
    
    // Característica TX (notificação para o cliente)
    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        NIMBLE_PROPERTY::NOTIFY
    );
    
    // Característica RX (escrita do cliente)
    NimBLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pRxCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
    
    // Inicia serviço
    pService->start();
    
    // Configura e inicia advertising
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
    
    bleInitialized = true;
    Serial.println("NimBLE OK: ESP32_LoRa");
    Serial.println("Aguardando conexao BLE...");
    
    while (1) {
        // Processa dados recebidos
        if (newBleData && bleRxBuffer.length() > 0) {
            newBleData = false;
            String msg = bleRxBuffer;
            bleRxBuffer = "";
            
            // Remove newline se houver
            msg.trim();
            
            if (msg.length() > 0) {
                // Encripta e envia via LoRa
                String toSend = encryptionEnabled ? encryptMessage(msg) : msg;
                Serial2.println(toSend);
                Serial.println("BLE->LoRa: " + msg);
                
                if (xSemaphoreTake(lvglMutex, portMAX_DELAY)) {
                    // Log na tela BT
                    String btLogEntry = "< " + msg + "\n> LoRa: OK\n";
                    lv_textarea_add_text(ui_bt_log, btLogEntry.c_str());
                    
                    // Também adiciona ao log LoRa (mostra que veio do BT)
                    String loraLogEntry = "[BT]> " + msg + "\n";
                    lv_textarea_add_text(ui_lora_log, loraLogEntry.c_str());
                    
                    xSemaphoreGive(lvglMutex);
                }
                
                // Echo de volta via BLE
                if (bleConnected && pTxCharacteristic != NULL) {
                    String echo = "Enviado via LoRa: " + msg;
                    pTxCharacteristic->setValue((uint8_t*)echo.c_str(), echo.length());
                    pTxCharacteristic->notify();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Task Bateria
void batteryTask(void *pvParameters) {
    while (1) {
        batteryVoltage = readBatteryVoltage();
        uint8_t percent = voltageToPercent(batteryVoltage);
        
        if (xSemaphoreTake(lvglMutex, portMAX_DELAY)) {
            // Atualiza tela de bateria se ativa
            if (currentScreen == SCREEN_BATTERY) {
                char voltStr[16];
                sprintf(voltStr, "%.2f V", batteryVoltage);
                lv_label_set_text(ui_battery_voltage, voltStr);
                lv_bar_set_value(ui_battery_bar, percent, LV_ANIM_ON);
                
                // Cor baseada no nível
                uint32_t color = 0x00FF00; // Verde
                if (percent < 20) color = 0xFF0000; // Vermelho
                else if (percent < 50) color = 0xFFAA00; // Laranja
                
                lv_obj_set_style_bg_color(ui_battery_bar, lv_color_hex(color), LV_PART_INDICATOR);
            }
            
            // Atualiza indicador no header (todas as telas)
            if (ui_header_battery != NULL) {
                char batStr[10];
                sprintf(batStr, "%.2fV", batteryVoltage);
                lv_label_set_text(ui_header_battery, batStr);
            }
            
            xSemaphoreGive(lvglMutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // Atualiza a cada 2 segundos
    }
}

// ============================================
// SETUP
// ============================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== LoRa Messenger + LVGL ===");
    Serial.println("Menu: 1=LoRa, 2=BT, 3=Bateria, 4=Crypto");

    // --- Configuração LoRa ---
    pinMode(LORA_M0, OUTPUT);
    pinMode(LORA_M1, OUTPUT);
    pinMode(LORA_AUX, INPUT);
    digitalWrite(LORA_M0, LOW);
    digitalWrite(LORA_M1, LOW);
    Serial2.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    Serial.println("LoRa UART iniciado");

    // --- Configuração Teclado ---
    initKeypad();
    Serial.println("Teclado 4x4 iniciado");

    // --- Configuração ADC Bateria ---
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(BATTERY_PIN, INPUT);
    Serial.println("ADC Bateria configurado");

    // Bluetooth será iniciado na task dedicada

    // --- Configuração TFT e LVGL ---
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    
    lvglMutex = xSemaphoreCreateMutex();
    
    lv_init();
    
    draw_buf = (uint8_t *)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!draw_buf) {
        Serial.println("ERRO: Falha ao alocar buffer!");
        while (1) delay(100);
    }

    lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_flush_cb(disp, disp_flush);
    lv_display_set_buffers(disp, draw_buf, NULL, BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // --- Cria todas as telas ---
    createMenuScreen();
    createLoRaScreen();
    createMonitorScreen();
    createBluetoothScreen();
    createBatteryScreen();
    
    // Inicia no menu
    lv_screen_load(ui_menu_screen);

    // --- Cria Tasks ---
    xTaskCreatePinnedToCore(lvglTickTask, "lvgl_tick", 2048, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(lvglTask, "lvgl_task", 16384, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(keypadTask, "keypad", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(loraTask, "lora", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(bluetoothTask, "bluetooth", 8192, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(batteryTask, "battery", 2048, NULL, 1, NULL, 0);

    Serial.println("Sistema Pronto!");
    Serial.println("Use o teclado matricial para navegar");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}