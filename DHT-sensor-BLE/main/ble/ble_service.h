#define BLE_SERVICE_UUID "12345678-1234-5678-1234-56789abcdef0"
#define BLE_CHAR_TEMP_UUID "12345678-1234-5678-1234-56789abcdef1"
#define BLE_CHAR_HUM_UUID "12345678-1234-5678-1234-56789abcdef2"

// Function to initialize the BLE service
void ble_service_init(void);

// Function to send temperature and humidity data to connected clients
void ble_service_send_data(float temperature, float humidity);