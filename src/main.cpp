//Libraries
#include "mbed.h"
#include "XBeeLib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#if defined(ENABLE_LOGGING)
    #include "DigiLoggerMbedSerial.h"   
    using namespace DigiLog;
#endif
using namespace XBeeLib;

//Time specifications
#define LED_TIME_ON_MS 4000
#define RECV_TIME_ON_MS 100
#define LDR_REFRESH_TIME_MS 2000
#define ULTRASOUND_REFRESH_TIME_MS 500
#define ULTRASOUND_SAMPLING_PERIOD_MS 100
#define SLEEP_TIME 5000
#define PING_REFRESH_TIME 15000
#define TRIAL_TIME 350

//Communication protocol
#define PING 0
#define PINGACK 10
#define LEDON 22
#define LEDONACK 33
typedef struct Message {
   uint8_t type;
   uint8_t destId;
   uint8_t srcId;
} Msg;

//Network parameters
#define NEIGHBOUR_LIMIT 10
uint8_t neighbours_lower[NEIGHBOUR_LIMIT] = {0}; 
uint8_t neighbours_upper[NEIGHBOUR_LIMIT] = {0};
uint8_t neigh_lower_power[NEIGHBOUR_LIMIT] = {0}; 
uint8_t neigh_upper_power[NEIGHBOUR_LIMIT] = {0};

//Init peripherics
AnalogIn ldr(p18);
PwmOut myled(p21);
AnalogIn sensor1(p17);
AnalogIn sensor2(p16);
XBee802 xbee = XBee802(RADIO_TX, RADIO_RX, RADIO_RESET, NC, NC, 9600);
DigitalOut sleep_req(RADIO_SLEEP_REQ);
DigitalIn on_sleep(RADIO_ON_SLEEP);

//FreeRTOS objects
SemaphoreHandle_t ledSemaphore;
SemaphoreHandle_t printSemaphore;
QueueHandle_t mQueue; 
QueueHandle_t ledQueue; 
#define QUEUE_SIZE 16

//LDR parameters
const float VDD = 3.3;
const int R1 = 10000;
#define LDR_THRESHOLD 1000
#define LDR_NUMBER_OF_SAMPLES 16
#define LDR_NUMBER_OF_MEASURES_TO_SLEEP 5
#define LDR_NUMBER_OF_MEASURES_TO_WAKE_UP 2

//LDR characteristics
float B_CONST = 0;
float M_SLOPE = 0;

//XBee parameters
uint8_t nodeID;
#define MAX_POWER_LEVEL 4
uint8_t curPowerLvl = 0;
uint8_t nextPowerLvl = 0;

//Ultrasound parameters
#define ULTRASOUND_NUMBER_OF_SAMPLES 5
#define THRESHOLD_DISTANCE 20
#define MAX_DISTANCE 50
bool flag_lower_turnON = false;
bool flag_upper_turnON = false;

//Filesystem path
LocalFileSystem local("local");

//Serial debugger
Serial pc(DEBUG_TX, DEBUG_RX);

//Protocol flags
bool flag_lower = false;
bool flag_upper = false;
#define NUMBER_TRIALS 3

//XBEE sender
static void send_data(Msg sendMsg, uint8_t powerLevel) {
    const uint8_t data_len = sizeof(sendMsg);
    const TxStatus txStatus = xbee.send_data_broadcast((const uint8_t *) &sendMsg, data_len);
    if (txStatus == TxStatusSuccess)
        pc.printf("0 Type:%d destId:%d srcId:%d powerLvl:%u\r\n",(int)(sendMsg.type), (int)(sendMsg.destId), (int)(sendMsg.srcId), powerLevel);
    else
        pc.printf("7 Send failed with %d\r\n", (int) txStatus);
}

//Receiver Task
void recvTask (void *pvParameters) {
    while(1){
        xbee.process_rx_frames();
        vTaskDelay(RECV_TIME_ON_MS/portTICK_RATE_MS);
    }
}

void ping_receive(uint8_t _source, uint8_t _powerLvl) {
    int i = 0;
    Msg ack;
    uint8_t tmp;
    uint8_t source = _source;
	uint8_t tmpPowerLvl = _powerLvl;
	RadioStatus radioStatus;
    
    //Check if neighbour is the next closest one
    if (source > nodeID){
        
        for (i = 0; i<NEIGHBOUR_LIMIT; i++){
            
            //Check if neighbour is already on list
            if(neighbours_upper[i] == source){
				
				//set antenna power level
                radioStatus = xbee.set_power_level(neigh_upper_power[i]);
                if (radioStatus != Success)
                    pc.printf("7 Error when setting Power Level\r\n");
                
                //Send acknowledgement message
                ack.type = PINGACK;
                ack.srcId = nodeID;
                ack.destId = _source;
                send_data(ack, neigh_upper_power[i]);
                return;
            }
        }
        
        //Bubble sort
        for (i = 0; i<NEIGHBOUR_LIMIT; i++) {
            // If the neighbour is invalid or the new ping is from a closer neighbour
            if (neighbours_upper[i] == 0 || source < neighbours_upper[i]){
                tmp = neighbours_upper[i];
                neighbours_upper[i] = source;
                source = tmp;
				
				//change also the power level
                tmp = neigh_upper_power[i];
                neigh_upper_power[i] = tmpPowerLvl;
                tmpPowerLvl = tmp;
            }	
        }
		
		//Print ID and position of new neighbour
		for (i = 0; i<NEIGHBOUR_LIMIT; i++) {
			if (neighbours_upper[i] == _source) {
				pc.printf("6 New upper neighbour (pos=%d, id=%u, powerLvl=%u)\r\n", i, _source, neigh_upper_power[i]);
				break;
			}
		}
    }
    
    //Check if neighbour is the former closest one
    else {
        
        //Check if neighbour is already on list
        for (i = 0; i<NEIGHBOUR_LIMIT; i++){
            if(neighbours_lower[i] == source){
				
				//set antenna power level
                radioStatus = xbee.set_power_level(neigh_lower_power[i]);
                if (radioStatus != Success)
                    pc.printf("7 Error when setting Power Level\r\n");
                
                // Send acknowledgement message
                ack.type = PINGACK;
                ack.srcId = nodeID;
                ack.destId = _source;
                send_data(ack, neigh_lower_power[i]);
                return;
            }
        }
        for (i = 0; i<NEIGHBOUR_LIMIT; i++){
            
            // If the neighbour is invalid or the new ping if from a closer neighbour
            if (neighbours_lower[i] == 0 || source > neighbours_lower[i]){
                tmp = neighbours_lower[i];
                neighbours_lower[i] = source;
                source = tmp;
				
				//change also the power level
                tmp = neigh_lower_power[i];
                neigh_lower_power[i] = tmpPowerLvl;
                tmpPowerLvl = tmp;
            }
        }
		
		//Print ID and position of new neighbour
		for (i = 0; i<NEIGHBOUR_LIMIT; i++) {
			if (neighbours_lower[i] == _source) {
				pc.printf("6 New lower neighbour (pos=%d, id=%u, powerLvl=%u)\r\n", i, _source, neigh_lower_power[i]);
				break;
			}
		}
		
    }
	
	//set antenna power level
    radioStatus = xbee.set_power_level(_powerLvl);
    if (radioStatus != Success)
        pc.printf("Error when setting Power Level\r\n");
    
    //Send ack back
    ack.type = PINGACK;
    ack.srcId = nodeID;
    ack.destId = _source;
    send_data(ack, _powerLvl);
}

void pingack_receive(uint8_t _source, uint8_t destination) {
    int i = 0;
    uint8_t tmp;
    uint8_t source = _source;
	uint8_t tmpPowerLvl = curPowerLvl; 
    
    //Check if neighbour is the next closest one
    if (source > nodeID){
        
        for (i = 0; i<NEIGHBOUR_LIMIT; i++){
            
            //Check if neighbour is already on list
            if(neighbours_upper[i] == source){
                return;
            }
        }
        
        //Bubble sort
        for (i = 0; i<NEIGHBOUR_LIMIT; i++){
            // If the neighbour is invalid or the new ping is from a closer neighbour
            if (neighbours_upper[i] == 0 || source < neighbours_upper[i]){
                tmp = neighbours_upper[i];
                neighbours_upper[i] = source;
                source = tmp;
				
				//change also the power level
                tmp = neigh_upper_power[i];
                neigh_upper_power[i] = tmpPowerLvl;
                tmpPowerLvl = tmp;
            }
        }
		
		//Print ID and position of new neighbour
		for (i = 0; i<NEIGHBOUR_LIMIT; i++) {
			if (neighbours_upper[i] == _source) {
				pc.printf("6 New upper neighbour (pos=%d, id=%u, powerLvl=%u)\r\n", i, _source, neigh_upper_power[i]);
				break;
			}
		}
    }
    
    //Check if neighbour is the former closest one
    else {
        
        //Check if neighbour is already on list
        for (i = 0; i<NEIGHBOUR_LIMIT; i++){
            if(neighbours_lower[i] == source){
                return;
            }
        }
        for (i = 0; i<NEIGHBOUR_LIMIT; i++){
            
            // If the neighbour is invalid or the new ping if from a closer neighbour
            if (neighbours_lower[i] == 0 || source > neighbours_lower[i]){
                tmp = neighbours_lower[i];
                neighbours_lower[i] = source;
                source = tmp;
				
				//change also the power level
                tmp = neigh_lower_power[i];
                neigh_lower_power[i] = tmpPowerLvl;
                tmpPowerLvl = tmp;
            }
        }
		
		//Print ID and position of new neighbour
		for (i = 0; i<NEIGHBOUR_LIMIT; i++) {
			if (neighbours_lower[i] == _source) {
				pc.printf("6 New lower neighbour (pos=%d, id=%u, powerLvl=%u)\r\n", i, _source, neigh_lower_power[i]);
				break;
			}
		}
		
    } 
}

void ledon_receive(uint8_t source, uint8_t destination) {
    Msg ack;
    bool PWMFlag = true;
	uint8_t powerLevel;
	int i;
    
    // Message was sent to this node
    if (destination == nodeID){
        
        // Turn on LED
        xQueueSend(ledQueue, (void *) &PWMFlag, 0); 
        xSemaphoreGive(ledSemaphore);
		
		//Find out which neighbour is it          
        if (nodeID < source) {
            for (i = 0; i < NEIGHBOUR_LIMIT; i++) {
                if(neighbours_upper[i] == source)  {
                    powerLevel = neigh_upper_power[i];
                    break;
                }
            }
        } else {
            for (i = 0; i < NEIGHBOUR_LIMIT; i++) {
                if(neighbours_lower[i] == source)  {
                    powerLevel = neigh_lower_power[i];
                    break;
                }
            }
        }
		
		//set antenna power level
        RadioStatus radioStatus = xbee.set_power_level(powerLevel);
        if (radioStatus != Success)
            pc.printf("7 Error when setting Power Level\r\n");
        
        // Send acknowledgement message
        ack.type = LEDONACK;
        ack.srcId = (uint8_t) nodeID;
        ack.destId = source;
        send_data(ack, powerLevel);
    }
    return;
}

void ledonack_receive(uint8_t source, uint8_t destination) {
    
    if (destination == nodeID) {
        
        //Update flags
        if (source > nodeID)
            flag_upper = true;
        else
            flag_lower = true;
    }
}

//Xbee receive callback function
static void receive_cb(const RemoteXBee802& remote, bool broadcast, const uint8_t *const data, uint16_t len) {

    //copy data to Msg
    Msg rcvMessage;
    memcpy(&rcvMessage, data, sizeof(Msg));
    
    //print the received message
    if (rcvMessage.destId == 255 || rcvMessage.destId == nodeID)
        pc.printf("1 Type:%d destId:%d srcId:%d\r\n",(int)(rcvMessage.type), (int)(rcvMessage.destId), (int)(rcvMessage.srcId));
    
    switch(rcvMessage.type){
        case PINGACK:
            pingack_receive(rcvMessage.srcId, rcvMessage.destId);
            break;
        case LEDON:
            ledon_receive(rcvMessage.srcId, rcvMessage.destId);
            break;
        case LEDONACK:
            ledonack_receive(rcvMessage.srcId, rcvMessage.destId);
            break;
        default:
			if(rcvMessage.type <= MAX_POWER_LEVEL)
                ping_receive(rcvMessage.srcId, rcvMessage.type);
            break;
    }
}

//Read from LDR (applies median filter)
float analogRead() {
  float sum=0;
  for(int i = 0; i < LDR_NUMBER_OF_SAMPLES; i++)
    sum += ldr.read()*VDD;
  return (sum/LDR_NUMBER_OF_SAMPLES);
}

//Calculates the Resistance [Ohm] of the LDR sensor based on Vin
float calc_R_ldr(float v_ldr){
  return (R1/v_ldr)*(VDD-v_ldr);
}

//Converts volt to LUXs
float calc_lux_from_v(float v_ldr) {
  float R = (R1/v_ldr)*(VDD-v_ldr);
  return pow(10.0, ((log10(R)-B_CONST)/M_SLOPE)*1.0);
}

//Task to turn led ON
void ledTask (void *pvParameters) {
    
    bool PWMFlag;
    
    while(1) {
        if (xSemaphoreTake(ledSemaphore, portMAX_DELAY) == pdTRUE) {
            
            //Check if led is to be turned on progressively or at max
            xQueueReceive(ledQueue, &PWMFlag, portMAX_DELAY);
            if (PWMFlag) {
                myled.write(0.25f);
                vTaskDelay((LED_TIME_ON_MS/4)/portTICK_RATE_MS);
                myled.write(0.50f);
                vTaskDelay((LED_TIME_ON_MS/4)/portTICK_RATE_MS);
                myled.write(0.75f);
                vTaskDelay((LED_TIME_ON_MS/4)/portTICK_RATE_MS);
                myled.write(1.00f);
                vTaskDelay((LED_TIME_ON_MS/4)/portTICK_RATE_MS);
            } else {
                myled.write(1.00f);
                vTaskDelay(LED_TIME_ON_MS/portTICK_RATE_MS);
            }
            
            //Turn off led
            myled = 0;
        }
    }
}

//task to read from LDR
void ldrTask (void *pvParameters) {
    
	RadioStatus radioStatus;
    int ldr_sleep_counter = 0, ldr_wake_up_counter = 0, i;
    bool sleep_flag = false;
    
    while(1) {
        
        //Read LDR
        float lux = calc_lux_from_v(analogRead());
        xSemaphoreTake(printSemaphore, portMAX_DELAY); 
        pc.printf("2 %.2f Lux\r\n", lux);
        xSemaphoreGive(printSemaphore);
        
        //Check if there is enough illumination right now
        if (lux > LDR_THRESHOLD) {
            ldr_sleep_counter++;    
            
            //Check if there has been enough illumination for some readings
            if(ldr_sleep_counter > LDR_NUMBER_OF_MEASURES_TO_SLEEP - 1) {
                if (!sleep_flag) {
                    sleep_flag = true;
                    pc.printf("3 Going to sleep\r\n");
                    sleep_req.write(1); //Sleep Xbee
                    vTaskSuspendAll(); //Suspend all threads except this one
                } else {
                    pc.printf("3 Keep sleeping!\r\n");
                }
                ldr_sleep_counter = 0;
                ldr_wake_up_counter = 0;
                wait_ms(SLEEP_TIME);
            }
        
        } else {
            ldr_sleep_counter = 0;
            
            //Check if it is sleeping
            if(sleep_flag) {
                ldr_wake_up_counter++;
                
                //Check if there has been not enough illumination for some readings
                if(ldr_wake_up_counter > LDR_NUMBER_OF_MEASURES_TO_WAKE_UP - 1) {
					
					//reset PING power level
                    curPowerLvl = 0;
                    nextPowerLvl = 0;
                
                    sleep_flag = false;
                    sleep_req.write(0); //Wake-up xbee
                    xTaskResumeAll(); //Recover all threads
                    pc.printf("3 Waking up!\r\n");
					
					//Reset all neighbours
                    for (i = 0; i<NEIGHBOUR_LIMIT; i++){
                        neighbours_lower[i] = 0;
                        neighbours_upper[i] = 0;
                    }
                    pc.printf("6 Resetting all neighbours\r\n");
					
					//set antenna power level
                    radioStatus = xbee.set_power_level(curPowerLvl);
                    if (radioStatus != Success)
                        pc.printf("7 Error when setting Power Level\r\n");

                    //Send ping
                    Msg msg;
                    msg.type = PING + nextPowerLvl;
                    msg.srcId = nodeID;
                    msg.destId = 255;
					send_data(msg, nextPowerLvl);
					
					//update power level
					curPowerLvl = nextPowerLvl;
					if(nextPowerLvl < MAX_POWER_LEVEL)
						nextPowerLvl++;
                    
                }
            }
            
        }
        
        //When all task are suspended, needs to call wait_ms instead of vTaskDelay
        if (sleep_flag)
            wait_ms(LDR_REFRESH_TIME_MS);
        else
            vTaskDelay(LDR_REFRESH_TIME_MS/portTICK_RATE_MS);
    }
}

//task to read from ultrasound
void ultrasoundTask (void *pvParameters){
    
    //Upper sensor
    float value_upper = 0, prevmeasure_upper = -1, measure_upper = 0;
    float measvect_upper[ULTRASOUND_NUMBER_OF_SAMPLES] = {0};
    
    //Lower sensor
    float value_lower = 0, prevmeasure_lower = -1, measure_lower = 0;
    float measvect_lower[ULTRASOUND_NUMBER_OF_SAMPLES] = {0};
    
    int i, j;
    bool direction, send_request = false; //0 -> upper, 1 -> lower
    float temp;
    bool PWMFlag = false;
    
    while(1){
        
        for (i = 0; i < ULTRASOUND_NUMBER_OF_SAMPLES; i++) {
            
            //Only has upper sensor
            if( flag_upper_turnON && !flag_lower_turnON) {
                value_upper = sensor1.read();
                measvect_upper[i] = value_upper*1024; //Both work at 3.3V
                
            //Only has lower sensor
            } else if (!flag_upper_turnON && flag_lower_turnON) {
                value_lower = sensor1.read();
                measvect_lower[i] = value_lower*1024; //Both work at 3.3V
            }
            
            //Has both sensors
            else {
                value_lower = sensor1.read();
                measvect_lower[i] = value_lower*1024; //Both work at 3.3V
                value_upper = sensor2.read();
                measvect_upper[i] = value_upper*1024; //Both work at 3.3V
            }
            vTaskDelay(ULTRASOUND_SAMPLING_PERIOD_MS/portTICK_RATE_MS);
        }
        
        //compute medians for upper sensor
        if (flag_upper_turnON) {
        
            for(i=0; i < ULTRASOUND_NUMBER_OF_SAMPLES-1; i++) {
                for(j=i+1; j<ULTRASOUND_NUMBER_OF_SAMPLES; j++) {
                    if(measvect_upper[j] < measvect_upper[i]) {
                        // swap elements
                        temp = measvect_upper[i];
                        measvect_upper[i] = measvect_upper[j];
                        measvect_upper[j] = temp;
                    }
                }
            }
            
            // get the element in the middle
            measure_upper = measvect_upper[(ULTRASOUND_NUMBER_OF_SAMPLES-1)/2]; 
            xSemaphoreTake(printSemaphore, portMAX_DELAY);      
            pc.printf("4 %.2f cm\r\n", measure_upper);
            xSemaphoreGive(printSemaphore);
        }
        
        //compute medians for lower sensor
        if (flag_lower_turnON) {
        
            for(i=0; i<ULTRASOUND_NUMBER_OF_SAMPLES-1; i++) {
                for(j=i+1; j<ULTRASOUND_NUMBER_OF_SAMPLES; j++) {
                    if(measvect_lower[j] < measvect_lower[i]) {
                        // swap elements
                        temp = measvect_lower[i];
                        measvect_lower[i] = measvect_lower[j];
                        measvect_lower[j] = temp;
                    }
                }
            }
            
            // get the element in the middle
            measure_lower = measvect_lower[(ULTRASOUND_NUMBER_OF_SAMPLES-1)/2];
            xSemaphoreTake(printSemaphore, portMAX_DELAY);         
            pc.printf("5 %.2f cm\r\n", measure_lower);
            xSemaphoreGive(printSemaphore);
        }
        
        //Check car was detected by upper sensor
        if (prevmeasure_upper != -1 && prevmeasure_upper-measure_upper >= THRESHOLD_DISTANCE && measure_upper <= MAX_DISTANCE) {
            
            //Add to LEDON queue
            direction = 1;
            xQueueSend(mQueue, (void *) &direction, 0); 
            send_request = true;
        }
        
        //Check car was detected by lower sensor
        if (prevmeasure_lower != -1 && prevmeasure_lower-measure_lower >= THRESHOLD_DISTANCE && measure_lower <= MAX_DISTANCE) {
            
            //Add to LEDON queue
            direction = 0;
            xQueueSend(mQueue, (void *) &direction, 0); 
            send_request = true;
        }
        
        if (send_request) {
            
            //Turn on own led
            xQueueSend(ledQueue, (void *) &PWMFlag, 0); 
            xSemaphoreGive(ledSemaphore);
        }
        
        //Update parameters
        prevmeasure_lower = measure_lower;
        prevmeasure_upper = measure_upper;
        vTaskDelay(ULTRASOUND_REFRESH_TIME_MS/portTICK_RATE_MS);
        send_request = false;
    }
}

//task to send broadcast if there are no neighbours
void neighbourTask(void *pvParameters) {
    
    //Ping message
    Msg msg;
    msg.srcId = nodeID;
    msg.destId = 255;
	RadioStatus radioStatus;
    
    //Send ping message if it has no neighbours
    while(1) {
        
		if ( (neighbours_lower[0] == 0 && flag_lower_turnON) || (neighbours_upper[0] == 0 && flag_upper_turnON)) {
            
			//update message type with encoded antennaPowerLevel
            msg.type = PING + nextPowerLvl;
			
			//Set antenna power level
            radioStatus = xbee.set_power_level(nextPowerLvl);
            if (radioStatus != Success) {
                pc.printf("7 Error when setting Power Level\r\n");
            }
						
			//Send ping message
			send_data(msg, nextPowerLvl);
			
			//update power level
            curPowerLvl = nextPowerLvl;
            if(nextPowerLvl < MAX_POWER_LEVEL)
                nextPowerLvl++;
			
		}
        vTaskDelay(PING_REFRESH_TIME/portTICK_RATE_MS);
    }
}

//task for managing messages retransmission
void retransmissionTask(void *pvParameters) {
    
    bool direction, upper, lower;
    Msg msg;
    msg.type = LEDON;
    msg.srcId = nodeID;
    int upper_counter = 0, lower_counter = 0, i = 0;
    BaseType_t bt;
	RadioStatus radioStatus;
    
    while(1) {
        
        //Check if both direction are on queue and simultaneously empty the queue
        upper = false;
        lower = false;
        
        xQueueReceive(mQueue, &direction, portMAX_DELAY);
        
        if (direction) //Upper
            upper = true;
        else //lower
            lower = true;
        
        bt = xQueueReceive(mQueue, &direction, 0);

        while((upper && neighbours_upper[0] != 0 && !flag_upper) || (lower && neighbours_lower[0] != 0 && !flag_lower) ) {
            
            //Empty queue
            while(bt == pdTRUE) { 
            
                if (direction) //Upper
                    upper = true;
                else //lower
                    lower = true;
            
                bt = xQueueReceive(mQueue, &direction, 0);
            } 
            
            //Send message to closer upper neighbour
            if (upper && neighbours_upper[0] != 0 && !flag_upper) {
                msg.destId = neighbours_upper[0];
				
				//set antenna power level
                radioStatus = xbee.set_power_level(neigh_upper_power[0]);
                if (radioStatus != Success)
                    pc.printf("7 Error when setting Power Level\r\n");
                
				//Send message
				send_data(msg, neigh_upper_power[0]);
            } 
            
            //Send message to closer lower neighbour
            if (lower && neighbours_lower[0] != 0 && !flag_lower) {
                msg.destId = neighbours_lower[0];
				
				//set antenna power level
                radioStatus = xbee.set_power_level(neigh_lower_power[0]);
                if (radioStatus != Success)
                    pc.printf("7 Error when setting Power Level\r\n");
                
				send_data(msg, neigh_lower_power[0]);
            } 
            vTaskDelay(TRIAL_TIME/portTICK_RATE_MS);
            
            if (!flag_upper && upper) {
                upper_counter++;
                if (upper_counter > NUMBER_TRIALS-1) {
                    upper_counter = 0;
                    
                    //Remove from upper neighbour list
					pc.printf("6 Upper neighbour (id=%u) removed due to time-out\r\n", neighbours_upper[0]);
                    for (i = 0; i < NEIGHBOUR_LIMIT - 2; i++) {
                        neighbours_upper[i] = neighbours_upper[i+1];
						neigh_upper_power[i] = neigh_upper_power[i+1];
					}
                    neighbours_upper[i] = 0;
					neigh_upper_power[i] = 0;
                }
            }   
            
            if (lower && !flag_lower) {
                lower_counter++;
                if (lower_counter > NUMBER_TRIALS-1) {
                    lower_counter = 0;
                    
                    //Remove from lower neighbour list
                    for (i = 0; i < NEIGHBOUR_LIMIT - 2; i++) {
                        neighbours_lower[i] = neighbours_lower[i+1];
						neigh_lower_power[i] = neigh_lower_power[i+1];
					}
                    neighbours_lower[i] = 0;
					neigh_lower_power[i] = 0;
                    pc.printf("6 Lower neighbour (id=%u) removed due to time-out\r\n", neighbours_lower[0]);
                }
            }
        }
            
        //Update parameters
        flag_upper = false;
        flag_lower = false;
        upper_counter = 0;
        lower_counter = 0;
    }
}

//Reads file from local filesystem
int read_node_parameters() {
    
    FILE *fp = fopen("/local/id.txt", "r");
    
    if(!fp) { 
        pc.printf("7 Could not open file!\r\n");
        return -1;
        
    } else {

        //Get M_SLOPE
        char buffer[128];
        fgets(buffer, 128, fp);
        sscanf(buffer, "%f", (float*) &M_SLOPE);

        //Get B_CONST
        fgets(buffer, 128, fp);
        sscanf(buffer, "%f", (float*) &B_CONST);
        
        //Get sensor configuration
        fgets(buffer, 128, fp);
        int conf;
        sscanf(buffer, "%d", (int*) &conf);
        
        switch(conf) {
            case 0:
                flag_upper_turnON = true;
                break;
            case 1:
                flag_lower_turnON = true;
                break;
            case 2:
                flag_upper_turnON = true;
                flag_lower_turnON = true;
                break;
            default:
                break;
        }
                
        //For debugging
        pc.printf("8 M_SLOPE: %.2f , B_CONST: %.2f, Sensor: %d\r\n", M_SLOPE, B_CONST, conf);
    
        //Close file
        fclose(fp); 
        return 0;
    } 
}

int main() {
    
    //Reset print
    pc.printf("9 \r\n");
    
    //read config file
    if( read_node_parameters() == -1) 
        return -1;
    
    //Configure led PWM
    myled.period(0.01f);

    //Register callback
    xbee.register_receive_cb(&receive_cb);
    
    //Set up xbee
    RadioStatus const radioStatus = xbee.init();
    MBED_ASSERT(radioStatus == Success);
    
    //Configure node ID
    char aux_nodeID[20];
    xbee.get_node_identifier(aux_nodeID);
    sscanf(aux_nodeID, "%u", (uint8_t*) &nodeID);
    pc.printf("8 This node has id: %u\r\n", nodeID);
     
    //Set up task manager
    xTaskHandle mainTaskHandler;

    //Set up tasks
    int taskError;
    taskError = xTaskCreate(ldrTask, "LDR Reader", 2*configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY+6, &mainTaskHandler);
    taskError = xTaskCreate(ledTask, "LED task", 2*configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY+5, &mainTaskHandler);
    taskError = xTaskCreate(ultrasoundTask, "Ultrasound task", 2*configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY+1, &mainTaskHandler);
    taskError = xTaskCreate(recvTask, "Radio Receiver", 2*configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY+3, &mainTaskHandler);   
    taskError = xTaskCreate(neighbourTask, "Neighbour Discovery", 2*configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY+4, &mainTaskHandler); 
    taskError = xTaskCreate(retransmissionTask, "Retransmission task", 2*configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY+2, &mainTaskHandler); 
    
    if(taskError == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY){
        pc.printf("7 Failed to start tasks\r\n");
        return -1;
    }
    
    //Create FreeRTOS objects
    ledSemaphore = xSemaphoreCreateBinary();
    printSemaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(printSemaphore);
    mQueue = xQueueCreate(QUEUE_SIZE, sizeof(bool));
    ledQueue = xQueueCreate(QUEUE_SIZE, sizeof(bool));

    //Start the scheduler
    vTaskStartScheduler();
    for(;;);
}