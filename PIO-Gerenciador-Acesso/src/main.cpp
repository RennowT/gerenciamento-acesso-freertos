#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <Preferences.h>

// Hardware definitions
#define LED_ROOM_1 13
#define LED_ROOM_2 14
#define BUTTON_ROOM_1 26
#define BUTTON_ROOM_2 25

// Definitions
#define DOOR_OPEN_TIME 5000

typedef struct {
    char name[20];
    char password[20];
    bool isAdmin;
} User; // Struct to represent a user

typedef struct {
    char userName[20];
    uint8_t doorNumber;
} Event; // Struct for door events

typedef enum {
    USER_DATA,
    EVENT_DATA
} DataType; // Enum to differentiate data types for Flash

typedef struct {
    DataType type;      // Data type identifier
    union {
        User user;      // User struct
        Event event;    // Event struct (to be implemented)
    } data;
} FlashMessage;         // Struct to store data for Flash

typedef enum {
    CLOSED,
    OPEN
} DoorState; // Enum to door state

// Global Variables
User users[10];
uint8_t usersCount = 0;
DoorState door1State = CLOSED;
DoorState door2State = CLOSED;
bool wifiConnected = false;

// Wi-Fi Credentials
const char* ssid = "";
const char* password = "";

// Webserver
AsyncWebServer server(80);

// HTML page with PROGMEM
const char webpage[] PROGMEM = R"rawliteral(
    <!DOCTYPE HTML>
    <html>
    <head>
      <title>Controle de Portas</title>
    </head>
    <body>
      <h1>Controle Remoto</h1>
      <button onclick="control(1)">Abrir Porta 1</button>
      <button onclick="control(2)">Abrir Porta 2</button>
      <script>
        function control(port) {
          const button = event.target;
          const action = button.innerHTML.includes("Abrir") ? "on" : "off";
          
          fetch(`/porta${port}/${action}`)
            .then(response => {
              button.innerHTML = action === "on" 
                ? `Fechar Porta ${port}` 
                : `Abrir Porta ${port}`;
            })
            .catch(error => console.log('Erro:', error));
        }
      </script>
    </body>
    </html>
    )rawliteral";

// FreeRTOS
QueueHandle_t flashQueue;           // Queue to store data for Flash
SemaphoreHandle_t flashSemaphore;   // Semaphore to protect Flash access
Preferences flashStorage;           // Flash storage object
SemaphoreHandle_t doorMutex;        // Semaphore to ports control
SemaphoreHandle_t wifiConnectedSemaphore;

// Function Prototypes
void taskMenu(void *pvParameter);
void displayMenu();
void registerUser();
void listUsers();
void listEvents();
int authenticateUser();
void controlDoor(uint8_t doorNumber, uint8_t ledPin, uint8_t buttonPin, DoorState* state);
void taskFlash(void *pvParameters);
void taskWebServer(void *pvParameters);

// Task 1: Menu - Manages the serial interface and user interactions
void taskMenu(void *pvParameter) {
    bool menuVisible = true; // Controls menu visibility state

    while (true) {
        // Waiting for Wi-Fi connection
        xSemaphoreTake(wifiConnectedSemaphore, portMAX_DELAY);

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
                case '2':
                    listUsers();    // Trigger user list flow
                    break;
                case '3': 
                    listEvents();   // Trigger event list flow
                    break;
                case '4':
                    controlDoor(1, LED_ROOM_1, BUTTON_ROOM_1, &door1State); // Trigger authentication flow and port 1 control
                    break;
                case '5': 
                    controlDoor(2, LED_ROOM_2, BUTTON_ROOM_2, &door2State); // Trigger authentication flow and port 2 control
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

    // Save user data to Flash
    FlashMessage msg;
    msg.type = USER_DATA;
    msg.data.user = newUser;

    if (xQueueSend(flashQueue, &msg, portMAX_DELAY) != pdTRUE) {
        Serial.println("\nErro ao salvar usuário!"); // Error message
        return;
    }

    xSemaphoreGive(flashSemaphore); // Release semaphore to trigger Flash task

    Serial.println("\nUsuário cadastrado com sucesso!"); // Success message
}

// Function to List Users
void listUsers() {
    // Check if there are no users
    if (usersCount == 0) {
        Serial.println("\nNenhum usuário cadastrado!");
        return;
    }

    Serial.println("\n=== USUÁRIOS CADASTRADOS ===");
    for(int i = 0; i < usersCount; i++) {
        Serial.print(i + 1);
        Serial.print(". Nome: ");
        Serial.print(users[i].name);
        Serial.print(" | Senha: ");
        Serial.print(users[i].password);
        Serial.print(" | Admin: ");
        Serial.println(users[i].isAdmin ? "Sim" : "Não");
    }
}

// Function to List Events
void listEvents() {
    // Authenticate user
    int userIndex = authenticateUser();
    if (userIndex < 0) {
        Serial.println("\nAcesso negado! Senha invalida.");
        return;
    } else if (!users[userIndex].isAdmin) {
        Serial.println("\nAcesso negado! Somente administradores.");
        return;
    }

    // Load event count from Flash
    Preferences flash;
    flash.begin("events", true); // Read-onlu mode

    uint8_t eventCount = flash.getUChar("event_count", 0); // Read event count from Flash
    if (eventCount == 0) {
        Serial.println("\nNenhum evento registrado!");
        flash.end(); // Close Flash access
        return;
    }

    Serial.println("\n=== EVENTOS ===");
    for(int i = 0; i < eventCount; i++) {
        Event evento;
        String key = "event_" + String(i);
        flash.getBytes(key.c_str(), &evento, sizeof(Event));

        // Busca usuário correspondente
        bool encontrado = false;
        User usuario;
        for(int j = 0; j < usersCount; j++) {
            if(strcmp(users[j].name, evento.userName) == 0) {
                usuario = users[j];
                encontrado = true;
                break;
            }
        }

        // Exibe dados formatados
        Serial.print(i+1);
        Serial.print(". Usuário: ");
        Serial.print(evento.userName);
        
        if(encontrado) {
            Serial.print(" (");
            Serial.print(usuario.isAdmin ? "Admin" : "Usuário");
            Serial.print(")");
        } else {
            Serial.print(" [Não cadastrado]");
        }
        
        Serial.print(" | Porta: ");
        Serial.println(evento.doorNumber);
    }
    
    flashStorage.end();
    
}

// Função de Autenticação
int authenticateUser() {
    while(Serial.available()) Serial.read(); // Clear serial buffer

    String inputPassword;
    Serial.print("\nDigite a senha:");

    // Collect password for authentication
    while (Serial.available() == 0) { vTaskDelay(10); }
    inputPassword = Serial.readStringUntil('\n');
    inputPassword.trim();

    // Checks if the password exists for any user
    for (int i = 0; i < usersCount; i++) {
        if (inputPassword.equals(users[i].password)) {
            return i; // Returns the user index
        }
    }

    return -1; // Authentication failed
}

void controlDoor(uint8_t doorNumber, uint8_t ledPin, uint8_t buttonPin, DoorState* state) {
    // Authenticate user
    int userIndex = authenticateUser();
    if (userIndex < 0) {
        Serial.println("\nAcesso negado! Senha invalida.");
        return;
    }

    // Create and send event
    FlashMessage eventMsg;
    eventMsg.type = EVENT_DATA;
    strncpy(eventMsg.data.event.userName, users[userIndex].name, sizeof(eventMsg.data.event.userName) - 1); // Copies the authenticated user name to the event 
    eventMsg.data.event.userName[sizeof(eventMsg.data.event.userName) - 1] = '\0';
    eventMsg.data.event.doorNumber = doorNumber;

    if (xQueueSend(flashQueue, &eventMsg, portMAX_DELAY) != pdTRUE) {
        Serial.println("\nErro ao registrar evento!");
    }
    xSemaphoreGive(flashSemaphore);

    // Open the door
    Serial.println("\nAcesso liberado!");
    digitalWrite(ledPin, HIGH);
    *state = OPEN;

    // 5 second timer
    uint32_t startTime = xTaskGetTickCount();
    while ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(DOOR_OPEN_TIME)) {
        // Checks if the button was pressed
        if (digitalRead(buttonPin) == LOW) { // Button pressed
            break;
        }
        vTaskDelay(50); // Does not block other tasks
    }

    // Close the door
    digitalWrite(ledPin, LOW);
    *state = CLOSED;
    Serial.println("\nPorta fechada.");
}

// Task 2: Flash - Manages data storage on Flash memory
void taskFlash(void *pvParameters) {
    FlashMessage receivedMsg; //Buffer to store received messages

    while (true) {
        // Wait Semaphore to access Flash
        xSemaphoreTake(flashSemaphore, portMAX_DELAY);

        // Process all messages in the queue
        while (xQueueReceive(flashQueue, &receivedMsg, 0) == pdTRUE) {
            switch (receivedMsg.type) {
                case USER_DATA: {
                    // Save user data to Flash
                    flashStorage.begin("users", false);                                         // Open Flash for writing
                    String key = "user_" + String(flashStorage.getUChar("count", 0));           // Generate key for user data
                    flashStorage.putBytes(key.c_str(), &receivedMsg.data.user, sizeof(User));   // Save data
                    flashStorage.putUChar("count", flashStorage.getUChar("count", 0) + 1);      // Increment user count
                    flashStorage.end();                                                         // Close Flash access
                    break;
                }

                case EVENT_DATA: {
                    // Save event data to Flash
                    flashStorage.begin("events", false);                                                // Open Flash for writing
                    String eventKey = "event_" + String(flashStorage.getUChar("event_count", 0));       // Generate key for event data
                    flashStorage.putBytes(eventKey.c_str(), &receivedMsg.data.event, sizeof(Event));    // Save data
                    flashStorage.putUChar("event_count", flashStorage.getUChar("event_count", 0) + 1);  // Increment event count
                    flashStorage.end();                                                                 // Close Flash access
                    break;
                }
            }
        }
    }
}

// Task 3: Webserver
void taskWebServer(void *pvParameters) {
    // Attempt to connect to configured WiFi network
    Serial.println("Conectando ao Wi-Fi...");
    WiFi.begin(ssid, password);

    // Wait persistently for connection establishment
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print(".");
    }

    // Connection established
    Serial.println("\nConectado! IP: ");
    Serial.println(WiFi.localIP());

    // HTTP Route Configuration
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", webpage); // Send pre-stored HTML
    });

    // Door 1 Control Endpoints
    server.on("/porta1/on", HTTP_GET, [](AsyncWebServerRequest *request){
        xSemaphoreTake(doorMutex, portMAX_DELAY);           // Acquire door mutex
        digitalWrite(LED_ROOM_1, HIGH);                     // Physically open door
        door1State = OPEN;                                  // Update state
        xSemaphoreGive(doorMutex);                          // Release mutex
        request->send(200, "text/plain", "Porta 1 aberta"); // Confirm action
    });

    server.on("/porta1/off", HTTP_GET, [](AsyncWebServerRequest *request){
        xSemaphoreTake(doorMutex, portMAX_DELAY);
        digitalWrite(LED_ROOM_1, LOW);                      // Physically close door
        door1State = CLOSED;
        xSemaphoreGive(doorMutex);
        request->send(200, "text/plain", "Porta 1 fechada");
    });

    // Door 2 Control Endpoints (mirror door 1 functionality)
    server.on("/porta2/on", HTTP_GET, [](AsyncWebServerRequest *request){
        xSemaphoreTake(doorMutex, portMAX_DELAY);
        digitalWrite(LED_ROOM_2, HIGH);
        door2State = OPEN;
        xSemaphoreGive(doorMutex);
        request->send(200, "text/plain", "Porta 2 aberta");
    });

    server.on("/porta2/off", HTTP_GET, [](AsyncWebServerRequest *request){
        xSemaphoreTake(doorMutex, portMAX_DELAY);
        digitalWrite(LED_ROOM_2, LOW);
        door2State = CLOSED;
        xSemaphoreGive(doorMutex);
        request->send(200, "text/plain", "Porta 2 fechada");
    });

    // Server Initialization
    wifiConnected = true;
    xSemaphoreGive(wifiConnectedSemaphore); // Release o semaphore
    server.begin(); // Start listening for incoming connections
    
    // Keep task alive with minimal resource usage
    while(true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Setup
void setup() {
    // Serial configuration
    Serial.begin(115200);

    // GPIO configuration
    pinMode(LED_ROOM_1, OUTPUT);
    pinMode(LED_ROOM_2, OUTPUT);
    pinMode(BUTTON_ROOM_1, INPUT_PULLUP);
    pinMode(BUTTON_ROOM_2, INPUT_PULLUP);

    digitalWrite(LED_ROOM_1, LOW);
    digitalWrite(LED_ROOM_2, LOW);

    // FreeRTOS configuration
    flashQueue = xQueueCreate(10, sizeof(FlashMessage));    // Queue for 10 itens
    flashSemaphore = xSemaphoreCreateBinary();              // Binary semaphore for Flash access
    doorMutex = xSemaphoreCreateMutex();                    // Mutex for door control
    wifiConnectedSemaphore = xSemaphoreCreateBinary();      // BInary semaphore for initialization Menu
    xSemaphoreTake(wifiConnectedSemaphore, 0);              // Starts blocked

    // Load users from Flash
    flashStorage.begin("users", true);                                  // Read-only mode
    usersCount = flashStorage.getUChar("count", 0);                     // Read user count from Flash
    for (int i = 0; i < usersCount; i++) {
        String key = "user_" + String(i);                               // Generate key for user data
        flashStorage.getBytes(key.c_str(), &users[i], sizeof(User));    // Recover data
    }
    flashStorage.end(); // Close Flash access

    // Load event count from Flash
    flashStorage.begin("events", true);                             // Read-onlu mode
    uint8_t eventCount = flashStorage.getUChar("event_count", 0);   // Read event count from Flash
    flashStorage.end(); // Close Flash access

    // Create tasks
    xTaskCreate(taskMenu, "Menu", 4096, NULL, 2, NULL);
    xTaskCreate(taskFlash, "Flash", 4096, NULL, 1, NULL);
    xTaskCreate(taskWebServer, "WebServer", 8192, NULL, 1, NULL);
}

void loop() {
    
}
