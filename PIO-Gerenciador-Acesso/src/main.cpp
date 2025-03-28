#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// Hardware definitions
#define LED_ROOM_1 13
#define LED_ROOM_2 14
#define BUTTON_ROOM_1 26
#define BUTTON_ROOM_2 35

typedef struct {
    char name[20];
    char password[20];
    bool isAdmin;
} User;

// Global Variables
User users[10];
uint8_t usersCount = 0;

// Function Prototypes
void taskMenu(void *pvParameter);
void displayMenu();
void registerUser();

// Task 1: Menu - Manages the serial interface and user interactions
void taskMenu(void *pvParameter) {
    bool menuVisible = true; // Controls menu visibility state

    while (true) {
        // Display menu only when required (avoids spamming the serial monitor)
        if (menuVisible) {
            displayMenu();
            menuVisible = false; // Prevent re-display until next interaction
        }

        // Check for user input
        if (Serial.available()) {
            char option = Serial.read();

            // Process selected option
            switch (option) {
                case '1':
                    registerUser(); // Trigger user registration flow
                    break;
                case '2': // Placeholder for user listing (to be implemented)
                    break;
                case '3': // Placeholder for event logging (to be implemented)
                    break;
                case '4': // Placeholder for door 1 control (to be implemented)
                    break;
                case '5': // Placeholder for door 2 control (to be implemented)
                    break;
                default:
                    Serial.println("\nOpção inválida!"); // User feedback for invalid input
                    break;
            }

            while(Serial.available()) Serial.read(); // Clear residual data from serial buffer
            menuVisible = true; // Re-enable menu display after action
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Non-blocking delay for FreeRTOS scheduler
    }
}

// Function to display the menu on the serial monitor
void displayMenu() {
    Serial.println("\n=== MENU PRINCIPAL ===");
    Serial.println("1 - Cadastrar Usuário");
    Serial.println("2 - Listar Usuários");
    Serial.println("3 - Listar Eventos");
    Serial.println("4 - Liberar porta 1");
    Serial.println("5 - Liberar porta 2");
    Serial.print("Escolha uma opção: "); // Prompt for user input
}

// Handles new user registration process
void registerUser() {
    // Check if user limit is reached
    if (usersCount >= 10) {
        Serial.println("\nNúmero máximo de usuários atingido!");
        return;
    }

    User newUser; // Temporary storage for new user data
    String input; // Buffer for serial input

    while(Serial.available()) Serial.read(); // Clear serial buffer

    // Start registration workflow
    Serial.println("\n=== CADASTRO DE USUÁRIO ===");

    // Collect user name
    Serial.println("Nome: ");
    while (Serial.available() == 0) { vTaskDelay(10); }
    input = Serial.readStringUntil('\n');
    input.trim();
    input.toCharArray(newUser.name, sizeof(newUser.name));

    // Collect password
    Serial.println("Senha: ");
    while (Serial.available() == 0) { vTaskDelay(10); }
    input = Serial.readStringUntil('\n');
    input.trim();
    input.toCharArray(newUser.password, sizeof(newUser.password));

    // Determine admin privileges
    Serial.println("Admin (1 - Sim, 0 - Não): ");
    while (Serial.available() == 0) { vTaskDelay(10); }
    input = Serial.readStringUntil('\n');
    newUser.isAdmin = (input.toInt() == 1);

    // Save user and provide feedback
    users[usersCount] = newUser;
    usersCount++;
    Serial.println("\nUsuário cadastrado com sucesso!"); // Success message
}

// Setup

void setup() {
    Serial.begin(115200);
    pinMode(LED_ROOM_1, OUTPUT);
    pinMode(LED_ROOM_2, OUTPUT);
    pinMode(BUTTON_ROOM_1, INPUT_PULLUP);
    pinMode(BUTTON_ROOM_2, INPUT_PULLUP);

    digitalWrite(LED_ROOM_1, LOW);
    digitalWrite(LED_ROOM_2, LOW);

    xTaskCreate(taskMenu, "Menu", 4096, NULL, 1, NULL);
}

void loop() {
    
}
