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

typedef struct timing_messaage{
    uint32_t t1; //Sender Timestamp A->B
    uint32_t t2; //Receiver recv Timestamp
    uint32_t t3; //Recevier send Timestamp B->A
    uint32_t time_start_loop; // micros
    bool recv = false; //Flag to indidate that this message was received
};

const uint8_t RECEIVER_NODE_MAC[] = {0xE8, 0x9F, 0x6D, 0x26, 0x09, 0x20};
const uint8_t SENDER_NODE_MAC[] = {0xE8, 0x9F, 0x6D, 0x25, 0x52, 0x50};

// Receiver: 0
// Sender: Syncs to the receiver
uint32_t clock_offset_us = 0;

CurrentMode g_node_mode;
timing_messaage esp_now_data;
esp_now_peer_info_t peerInfo;


CurrentMode check_sender_recv();
bool init_esp_now();


void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);

    g_node_mode = check_sender_recv();

    if(g_node_mode == CurrentMode::SENDER) Serial.println("SENDER!");
    else if(g_node_mode == CurrentMode::RECEIVER) Serial.println("RECEIVER!");
    else Serial.println("UNKNOWN!");

    if(!init_esp_now())
    {
        Serial.print("Failed ESP_NOW init"); while(1);
    }


}

void loop() {

}

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