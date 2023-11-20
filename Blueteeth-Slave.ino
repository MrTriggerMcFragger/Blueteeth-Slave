#include "Blueteeth-Slave.h"

char input_buffer[MAX_BUFFER_SIZE];
SemaphoreHandle_t uartMutex;
TaskHandle_t terminalInputTaskHandle;
TaskHandle_t packetReceptionTaskHandle;

terminalParameters_t terminalParameters;
int discoveryIdx;

BluetoothA2DPSource a2dpSource;

BlueteethBaseStack internalNetworkStack(10, &packetReceptionTaskHandle, &Serial2, &Serial1);
BlueteethBaseStack * internalNetworkStackPtr = &internalNetworkStack; //Need pointer for run-time polymorphism

#ifdef TIME_STREAMING
extern uint32_t streamTime; //TEMPORARY DEBUG VARIABLE (REMOVE LATER)
#endif

// callback 
int32_t a2dpSourceDataRetrieval(Frame * frames, int32_t frameCount) {
  
  
  // delay(1); //delay for 3 milliseconds to allow 128 more bytes to come in

  // Serial.printf("Buffer size is %d and the requested data is %d\n\r", internalNetworkStack.dataBuffer.size(), frameCount);
  int realDataInsertionIncrement; //How many zeroes need to be placed prior to a real sample being inserted
  int samplesInBuffer = internalNetworkStack.dataBuffer.size()/4; //2 bytes per sample, 2 samples (one for each channel) per frame

  switch(samplesInBuffer){
    
    case 0:
      realDataInsertionIncrement = frameCount + 1; //never insert real data
      break;

    default:
      realDataInsertionIncrement = 1; //insert real data each step
      int zeroFrames = frameCount - samplesInBuffer;
      if (zeroFrames > 0){
        realDataInsertionIncrement = ceil((float)(frameCount - samplesInBuffer) / samplesInBuffer) + 1;
      }
      break;
    
  }

  for (int i = 0; i < frameCount; i++){

    if ( ( (i + 1) % realDataInsertionIncrement ) != 0 ){ //stuff zeroes
      frames[i].channel1 = 0;
      frames[i].channel2 = 0;
    }
    else {
      frames[i].channel1 = internalNetworkStack.dataBuffer.front(); internalNetworkStack.dataBuffer.pop_front();
      frames[i].channel1 += internalNetworkStack.dataBuffer.front() << 8; internalNetworkStack.dataBuffer.pop_front();
      frames[i].channel2 = internalNetworkStack.dataBuffer.front(); internalNetworkStack.dataBuffer.pop_front();
      frames[i].channel2 += internalNetworkStack.dataBuffer.front() << 8; internalNetworkStack.dataBuffer.pop_front();
    }
  
  }

  return frameCount;
  
}

// callback 
// int32_t a2dpSourceDataRetrieval(Frame * frames, int32_t frameCount) {
  

//   int samplesAvailable = min(internalNetworkStack.dataBuffer.size()/4, (size_t) frameCount);

//   // Serial.printf("There are %d samples currently available - Attempting to take out %d\n\r", internalNetworkStack.dataBuffer.size()/4, samplesAvailable);

//   for (int i = 0; i < samplesAvailable; i++){

//       frames[i].channel1 = internalNetworkStack.dataBuffer.front(); internalNetworkStack.dataBuffer.pop_front();
//       frames[i].channel1 += internalNetworkStack.dataBuffer.front() << 8; internalNetworkStack.dataBuffer.pop_front();
//       frames[i].channel2 = internalNetworkStack.dataBuffer.front(); internalNetworkStack.dataBuffer.pop_front();
//       frames[i].channel2 += internalNetworkStack.dataBuffer.front() << 8; internalNetworkStack.dataBuffer.pop_front();
//   }

//   return samplesAvailable;
  
// }

void setup() {
  
  //Start Serial comms
  Serial.begin(115200);
  uartMutex = xSemaphoreCreateMutex(); //mutex for uart

  internalNetworkStack.begin();

  //Create tasks
  xTaskCreate(terminalInputTask, // Task function
  "UART TERMINAL INPUT", // Task name
  4096, // Stack size 
  NULL, 
  1, // Priority
  &terminalInputTaskHandle); // Task handler

  xTaskCreate(packetReceptionTask, // Task function
  "PACKET RECEPTION HANDLER", // Task name
  4096, // Stack size 
  NULL, 
  1, // Priority
  &packetReceptionTaskHandle); // Task handler

  // a2dpSource.set_auto_reconnect(true);
  // a2dpSource.start("SRS-XB13", a2dpSourceData); 
  // a2dpSource.set_volume(10);

}

void loop() {
  // Serial1.print("UART working!\n\r");
  // delay(200);
}


inline void int2Bytes(uint32_t integer, uint8_t * byteArray){
  for (int offset = 0; offset < 32; offset += 8){
    byteArray[offset/8] = integer >> offset; //assignment will truncate so only first 8 bits are assigned
  }
}

/*  Unpacks byte array into a 32 bit integer
*   
*   @byteArray - array containing 4 bytes corresponding to a 32 bit integer
*   @return - the resulting integer
*/  
inline uint32_t bytes2Int(uint8_t * byteArray){
  uint32_t integer = 0;
  for (int offset = 0; offset < 32; offset += 8){
    integer += byteArray[offset/8] << offset; 
  }
  return integer;
}

/*  Performs a checksum of all bytes in the buffer
*   
*   @buffer - buffer containing bytes
*   @return - the checksum of the byte buffer
*/  
uint32_t inline byteBufferCheckSum(deque<uint8_t> buffer){
  uint32_t sum = 0;
  for (int i = 0; i < buffer.size(); i++){
    sum += buffer[i];
  }
  return sum;
}

/*  Task that runs when a new Blueteeth packet is received. 
*
*/  
void packetReceptionTask (void * pvParams){
  while(1){
    vTaskSuspend(packetReceptionTaskHandle);
    Serial.print("Processing received packet...\n\r");
    BlueteethPacket packetReceived = internalNetworkStack.getPacket();

    uint8_t srcAddr = internalNetworkStack.getAddress();
    BlueteethPacket response(false, srcAddr, 0); //Need to declare prior to switch statement to avoid "crosses initilization" error.

    switch(packetReceived.type){
      case CONNECT:
        a2dpSource.set_auto_reconnect(true);
        Serial.print("Set autoreconnect... ");
        a2dpSource.start( (char *) packetReceived.payload, a2dpSourceDataRetrieval); 
        Serial.print("Attempting to connect... ");
        // a2dpSource.set_volume(10);
        // Serial.print("Set volume...");
        Serial.print("\n\r");
        break;

      case DISCONNECT:
        a2dpSource.set_auto_reconnect(false);
        Serial.print("Unsetting autoreconnect... ");
        a2dpSource.disconnect(); 
        Serial.print("Disconnecting... ");
        // a2dpSource.set_volume(10);
        // Serial.print("Set volume...");
        Serial.print("\n\r");
        break;

      case PING:
        Serial.print("Ping packet type received. Responding...\n\r"); //DEBUG STATEMENT
        response.type = PING;
        sprintf( (char *) response.payload, "%d", srcAddr);
        Serial.printf("The packet payload before sending is %s\n\r", (char *) response.payload);
        internalNetworkStack.queuePacket(true, response);
        break;

      case STREAM: {
        response.type = STREAM_RESULTS;
        while(internalNetworkStack.dataBuffer.size() < 39990){ 
          //do nothing 
        } //Wait till the buffer is at least 99% full
        uint32_t checkSum = byteBufferCheckSum(internalNetworkStack.dataBuffer);
        int2Bytes(checkSum, response.payload);
        #ifdef TIME_STREAMING
        int2Bytes(streamTime, response.payload + 4);
        #endif
        internalNetworkStack.queuePacket(true, response);
        internalNetworkStack.dataBuffer.resize(0);
        break;
      }

      default:
        Serial.print("Unknown packet type received.\n\r"); //DEBUG STATEMENT
        break;
    }

  }
} 

/*  Prints all characters in a character buffer
*
*   @endPos - last buffer position that should be printed
*/  
void inline printBuffer(int endPos){

  Serial.print("\0337"); //save cursor positon
  Serial.printf("\033[%dF", endPos + 1); //go up N + 1 lines
  for (int i = 0; i <= endPos; i++) {

    Serial.print("\033[2K"); //clear line
    
    switch(input_buffer[i]){
      case '\0':
        Serial.printf("Character %d = NULL\n\r", i);
        break;
      case '\n':
        Serial.printf("Character %d = NEWLINE\n\r", i);
        break;
      case 127:
        Serial.printf("Character %d = BACKSPACE\n\r", i);
        break;
      default:
        Serial.printf("Character %d = %c\n\r", i , input_buffer[i]);
    }
  }
  Serial.print("\0338"); //restore cursor position
}

/*  Take in user inputs and handle pre-defined commands.
*
*/
void terminalInputTask(void * params) {

  clear_buffer(input_buffer, sizeof(input_buffer));
  int buffer_pos = 0;
  const char * btTarget;
  
  while(1){

    vTaskDelay(100);

    xSemaphoreTake(uartMutex, portMAX_DELAY);

    while (Serial.available() && (buffer_pos < MAX_BUFFER_SIZE)){ //get number of bits on buffer
      
      input_buffer[buffer_pos] = Serial.read();

      //handle special chracters
      if (input_buffer[buffer_pos] == '\r') { //If an enter character is received
        
        input_buffer[buffer_pos] = '\0'; //Get rid of the carriage return
        Serial.print("\n\r");
        
        // printBuffer(buffer_pos);

        switch ( handle_input(input_buffer, terminalParameters) ){
          case CONNECT:

            a2dpSource.set_auto_reconnect(true);
            Serial.print("Set autoreconnect... ");
            a2dpSource.start("SRS-XB13", a2dpSourceDataRetrieval); 
            Serial.print("Attempting to connect... ");
            // a2dpSource.set_volume(10);
            // Serial.print("Set volume...");
            Serial.print("\n\r");
            break;
          
          case DISCONNECT:

            a2dpSource.set_auto_reconnect(false);
            Serial.print("Unsetting autoreconnect... ");
            a2dpSource.disconnect(); 
            Serial.print("Disconnecting... ");
            // a2dpSource.set_volume(10);
            // Serial.print("Set volume...");
            Serial.print("\n\r");
            break;

          case TEST:
            // Serial.printf("Address = %d, Checksum = %lu\n\r", internalNetworkStack.getAddress(), byteBufferCheckSum(internalNetworkStack.dataBuffer));
            Serial.printf("Buffer Size = %d, Connection Status = %d\n\r", internalNetworkStack.dataBuffer.size(), a2dpSource.is_connected());
            // vTaskResume(packetReceptionTaskHandle);
            break;

          case STREAM:
            break;
          case FLUSH:
            internalNetworkStack.flushDataPlaneSerialBuffer();
            break;

          default:
            break;
            //no action needed

        } //handle the input
        
        
        clear_buffer(input_buffer, sizeof(input_buffer));
        buffer_pos = -1; //return the buffer back to zero (incrimented after this statement)
      }
      
      else if (input_buffer[buffer_pos] == 127){ //handle a backspace character
        Serial.printf("%c", 127); //print out backspace
        input_buffer[buffer_pos] = '\0'; //clear the backspace 
        if (buffer_pos > 0) input_buffer[--buffer_pos] = '\0'; //clear the previous buffer pos if there was another character in the buffer that wasn't a backspace
        buffer_pos--;
      }
      
      else Serial.printf("%c", input_buffer[buffer_pos]);

      buffer_pos++;
      
    }

    xSemaphoreGive(uartMutex);

  }
}