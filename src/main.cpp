/**
 * @file main.cpp
 * @author Samuel Yow
 * @date 2025-03-17
 * @brief POC time synchronization device
 */
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>

enum CurrentMode{
    SENDER = 0,
    RECEIVER = 1,
    NONE = 2,
};

//State machine to represent the current state //TODO
enum CurrentState{
    BROADCASTING = 0,
    WAITING_REPLY = 1,
    MQTT = 2
};

typedef struct timing_messaage{
    double t1; //Sender Timestamp A->B
    double t2; //Receiver recv Timestamp
    double t3; //Recevier send Timestamp B->A
    double t4; //When the receiver receives the packet

    double time_start_loop; // micros, B decides when to start sending the LED
}Timing_Message;

bool g_msg_recv = false;

const uint8_t RECEIVER_NODE_MAC[] = {0xE8, 0x9F, 0x6D, 0x26, 0x09, 0x20};
const uint8_t SENDER_NODE_MAC[] = {0xE8, 0x9F, 0x6D, 0x25, 0x52, 0x50};

CurrentMode g_node_mode;
Timing_Message esp_now_data;
esp_now_peer_info_t peerInfo;


CurrentMode check_sender_recv();
bool init_esp_now();


void cb_on_espnow_recv(const uint8_t * mac, const uint8_t *incomingData, int len);
bool send_esp_now_data();

void pixel_blue();
void pixel_red();
void pixel_green();


Adafruit_NeoPixel pixels(1, GPIO_NUM_0, NEO_GRB + NEO_KHZ800);

void setup() {
    Serial.begin(115200);

    //Blinking LED demo
    pinMode(GPIO_NUM_13, OUTPUT);
    digitalWrite(GPIO_NUM_13, LOW);

    // Enable NEOPIXEL LED for state machine
    pinMode(GPIO_NUM_2, OUTPUT);
    digitalWrite(GPIO_NUM_2, HIGH);

    pixels.setBrightness(20);
    pixels.clear();
    pixels.begin();
    
    WiFi.mode(WIFI_STA);

    g_node_mode = check_sender_recv();

    if(g_node_mode == CurrentMode::SENDER) Serial.println("SENDER!");
    else if(g_node_mode == CurrentMode::RECEIVER) Serial.println("RECEIVER!");
    else Serial.println("UNKNOWN!");

    if(!init_esp_now())
    {
        Serial.print("Failed ESP_NOW init"); while(1);
    }

    // Receiver: 0
    // Sender: Syncs to the receiver
    double clock_offset_us = 0;

    if(g_node_mode == CurrentMode::SENDER)
    {
        //Send until successful, i.e the receiver is online
        //need to check if this works...?
        
        pixel_red(); //Waiting for Pair to come online

        do
        {
            send_esp_now_data();
            delay(1000);

        }while((g_msg_recv == false));

        //Calculate Clock Offset
        double first_val = esp_now_data.t2 - esp_now_data.t1;
        double sec_val = esp_now_data.t4 - esp_now_data.t3;
        clock_offset_us = (first_val-sec_val)/2;
    }
    else //g_node_mode == CurrentMode::RECIEVER
    {
        //Wait until it gets a message from the sender

        pixel_red(); //Waiting for Pair to come online

        while(g_msg_recv == false)
        {
            delay(10);
        }

        //Start loop 10s later
        esp_now_data.time_start_loop = micros() + 10*1000000;
        send_esp_now_data();
    }

    pixel_green(); //Message Exchange Done, waiting for loop to start

    Serial.println("t1: " + String(esp_now_data.t1));
    Serial.println("t2: " + String(esp_now_data.t2));
    Serial.println("t3: " + String(esp_now_data.t3));
    Serial.println("t4: " + String(esp_now_data.t4));
    Serial.println("Start Loop: " + String(esp_now_data.time_start_loop));

    Serial.println("Offset: " + String(clock_offset_us));
    
    // Wait until esp_now_data.time_start_loop to start sending data
    double curr_time;
    while(1)
    {
        curr_time = (double)micros() + clock_offset_us;

        if (curr_time >= esp_now_data.time_start_loop)
        {
            break;
        }
    }

    pixel_blue(); //Loop Starts;
}

void loop() {
    static bool led_on = false;
    digitalWrite(GPIO_NUM_13, led_on);
    led_on = !led_on;

    delay(500);

}

/**
 * @brief Init ESP-NOW and add peer to peerInfo
 * 
 * @return true 
 * @return false 
 */
bool init_esp_now()
{

    if(esp_now_init() != ESP_OK)
    {
        Serial.println("Failed to init ESP-NOW");
        return false;
    }

    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if(g_node_mode == CurrentMode::NONE)
    {
        Serial.println("Unknown MAC");
        return false;
    }
    else if(g_node_mode ==CurrentMode::RECEIVER) //Add sender as peer
    {
        memcpy(peerInfo.peer_addr, SENDER_NODE_MAC, 6);
    }
    else  //(g_node_mode ==CurrentMode::SENDER) - add RECEIVER as peer
    {
        memcpy(peerInfo.peer_addr, RECEIVER_NODE_MAC, 6);
    }

    if (esp_now_add_peer(&peerInfo) != ESP_OK){
        Serial.println("Failed to add peer");
        return false;
    }

    esp_now_register_recv_cb(esp_now_recv_cb_t(cb_on_espnow_recv));

    return true;
}


/**
 * @brief 
 * 
 * @return uint8_t 0 - RECV, 1 - SENDER, 2 None
 */
CurrentMode check_sender_recv()
{
    uint8_t baseMac[6];
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);

    if(ret != ESP_OK)
    {
        Serial.println("Unable to get MAC address");
        return CurrentMode::NONE;
    }

    CurrentMode mode = CurrentMode::NONE;

    // Check if current mcu is receiver
    for(int i=0; i < 6; i++)
    {
        if(baseMac[i] == RECEIVER_NODE_MAC[i])
        {
            mode = CurrentMode::RECEIVER;
        }
        else
        {
            mode = CurrentMode::NONE;
            break;
        }
    }

    if(mode == CurrentMode::RECEIVER) return mode;

    // Check if current mcu is sender
    for(int i=0; i < 6; i++)
    {
        if(baseMac[i] == SENDER_NODE_MAC[i])
        {
            mode = CurrentMode::SENDER;
        }
        else
        {
            mode = CurrentMode::NONE;
            break;
        }
    }

    return mode;

}

/**
 * @brief Callback that is called when ESP-NOW-data is received
 * 
 * @param mac 
 * @param incomingData 
 * @param len 
 */
void cb_on_espnow_recv(const uint8_t * mac, const uint8_t *incomingData, int len)
{
    double recv_time = (double) micros();

    memcpy(&esp_now_data, incomingData, sizeof(esp_now_data));
    
    if(g_node_mode == CurrentMode::SENDER) esp_now_data.t4 = recv_time;
    else /*CurrentMode::RECIVER*/          esp_now_data.t2 = recv_time;

    g_msg_recv = true;


}

/**
 * @brief Send esp_now_data to the peer
 * 
 * @return true result = ESP_OK
 * @return false 
 */
bool send_esp_now_data()
{
    esp_err_t result;

    if(g_node_mode == CurrentMode::SENDER){
        esp_now_data.t1 = (double) micros();
        result = esp_now_send(RECEIVER_NODE_MAC,
            (uint8_t *) &esp_now_data, sizeof(esp_now_data));
    }
    else{
        esp_now_data.t3 = (double) micros();
        result = esp_now_send(SENDER_NODE_MAC,
            (uint8_t *) &esp_now_data, sizeof(esp_now_data));
    }

    return (result == ESP_OK);

}


void pixel_blue()
{
    pixels.setPixelColor(0, pixels.Color(0,0,100));
    pixels.show();
}

void pixel_red()
{
    pixels.setPixelColor(0, pixels.Color(100,0,0));
    pixels.show();
}

void pixel_green()
{
    pixels.setPixelColor(0, pixels.Color(0,100,0));
    pixels.show();
}