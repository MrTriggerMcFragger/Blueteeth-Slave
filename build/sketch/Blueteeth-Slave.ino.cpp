#include <Arduino.h>
#line 1 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
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

/*  Callback for sending data to A2DP BT stream
*   
*   @data - Pointer to the data that needs to be populated.
*   @len - The number of bytes requested.
*   @return - The number of frames populated.
*/ 
#line 26 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
int32_t a2dpSourceDataRetrieval(uint8_t * data, int32_t len);
#line 63 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
int32_t a2dpDirectTransfer(uint8_t * data, int32_t len);
#line 77 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
int32_t a2dpSourceDataRetrievalAlt(uint8_t * data, int32_t len);
#line 106 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
int32_t a2dpSourceDataRetrievalNoZeroes(uint8_t * data, int32_t len);
#line 124 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
int32_t cycleBuffer(uint8_t * data, int32_t len);
#line 161 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
void setup();
#line 186 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
void loop();
#line 196 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
void int2Bytes(uint32_t integer, uint8_t * byteArray);
#line 207 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
uint32_t bytes2Int(uint8_t * byteArray);
#line 231 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
void packetReceptionTask(void * pvParams);
#line 340 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
void terminalInputTask(void * params);
#line 26 "C:\\Users\\ztzac\\Documents\\GitHub\\Blueteeth-Slave\\Blueteeth-Slave.ino"
int32_t a2dpSourceDataRetrieval(uint8_t * data, int32_t len) {
  
  // Serial.printf("Buffer size is %d and the requested data is %d\n\r", internalNetworkStack.dataBuffer.size(), len);
  int realDataInsertionIncrement; 
  int samplesInBuffer = internalNetworkStack.dataBuffer.size(); 

  if (samplesInBuffer == 0){
    realDataInsertionIncrement = len + 1; //never insert real data
  }
  else {
      realDataInsertionIncrement = 1; //insert real data each step
      if ((len - samplesInBuffer) > 0){
        realDataInsertionIncrement = ceil((float)(len - samplesInBuffer) / samplesInBuffer) + 1;
      }
  }

  for (int i = 0; i < len; i++){

    if ( ( (i + 1) % realDataInsertionIncrement ) != 0 ){ //stuff zeroes
      data[i] = 0;
    }
    else {  //place real data
      data[i] = internalNetworkStack.dataBuffer.front(); internalNetworkStack.dataBuffer.pop_front();
    }
  
  }

  return len;
  
}

/*  Callback for sending data to A2DP BT stream directly from the serial buffer
*   
*   @data - Pointer to the data that needs to be populated.
*   @len - The number of bytes requested.
*   @return - The number of frames populated.
*/ 
int32_t a2dpDirectTransfer(uint8_t * data, int32_t len) {
  
  int32_t dataAvailable = min(len, internalNetworkStack.dataPlane->available()); 
  internalNetworkStack.dataPlane -> readBytes(data, dataAvailable);
  return dataAvailable;
  
}

/*  Callback for sending data to A2DP BT stream (BEST SO FAR)
*   
*   @data - Pointer to the data that needs to be populated.
*   @len - The number of bytes requested.
*   @return - The number of frames populated.
*/ 
int32_t a2dpSourceDataRetrievalAlt(uint8_t * data, int32_t len) {
  
  int bytesInBuffer = internalNetworkStack.dataBuffer.size(); 
  int zeroEntries = (len - bytesInBuffer);
  if (zeroEntries > 0){
    zeroEntries += (zeroEntries % 2); //Make sure there are an even number as 2 bytes per sample
    zeroEntries += (zeroEntries % 4); //Make sure there are audio samples for each channel
    bytesInBuffer = len - zeroEntries; //update bytes in buffer as this is how many bytes will be streamed
    for (int i = 0; i < zeroEntries; i++) data[i] = 0;
  }
  else {
    zeroEntries = 0;
    bytesInBuffer = min(bytesInBuffer, len);
  }

  for (int i = 0; i < bytesInBuffer; i++){
    data[zeroEntries + i] = internalNetworkStack.dataBuffer.front(); internalNetworkStack.dataBuffer.pop_front();
  }  

  return len;
  
}

/*  Callback for sending data to A2DP BT stream
*   
*   @data - Pointer to the data that needs to be populated.
*   @len - The number of bytes requested.
*   @return - The number of frames populated.
*/ 
int32_t a2dpSourceDataRetrievalNoZeroes(uint8_t * data, int32_t len) {

  // Serial.printf("%d bytes requested", len);

  int end = min(internalNetworkStack.dataBuffer.size(), (size_t) len);
  end -= end % 2; //make sure an even number of audio samples are sent at any given time
  for (int i = 0; i < end; i++){
    internalNetworkStack.dataBuffer.front(); internalNetworkStack.dataBuffer.pop_front();
  }  
  return end;
}

/*  Callback for cycling the buffer
*   
*   @data - Pointer to the data that needs to be populated.
*   @len - The number of bytes requested.
*   @return - The number of frames populated.
*/ 
int32_t cycleBuffer(uint8_t * data, int32_t len) {

  static int cnt = 0;

  int end = min(internalNetworkStack.dataBuffer.size(), (size_t) len); 

  for (int i = 0; i < end; i++){
    data[i] = internalNetworkStack.dataBuffer.at(cnt); //internalNetworkStack.dataBuffer.pop_front();
    cnt = ( cnt + 1 ) % internalNetworkStack.dataBuffer.size();
  }

  return end;
  
}



/*  Testfunction to ensure data stream works (plays pre-recorded data)
*   
*   @data - Pointer to the data that needs to be populated.
*   @len - The number of bytes requested.
*   @return - The number of frames populated.
*/ 
// int32_t streamPianoSamples(uint8_t * frames, int32_t frameCount) {
  
//   static size_t cnt = 0;

//   int i = 0;
//   while (i < frameCount){
//     frames[i++] = piano16bit_raw[cnt];
//     cnt = ( cnt + 1 ) % sizeof(piano16bit_raw);
//   }

//   return frameCount;
  
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

}

void loop() {

}

/*  Gets individual bytes of a 32 bit integer
*   
*   @integer - the integer being analyzed
*   @byteArray - array containing 4 bytes corresponding to a 32 bit integer
*   @return - the resulting integer
*/  
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
        #ifdef DIRECT_TRANSFER
        a2dpSource.start_raw( (char *) packetReceived.payload, a2dpDirectTransfer); 
        #else
        a2dpSource.start_raw( (char *) packetReceived.payload, a2dpSourceDataRetrievalAlt); 
        #endif
        Serial.print("Attempting to connect... ");
        // a2dpSource.set_volume(10);
        // Serial.print("Set volume...");
        Serial.print("\n\r");
        break;

      case DROP:
            Serial.print("Dropping one packet...\n\r");
            internalNetworkStack.dataBuffer.pop_front(); //get rid of one byte
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
        // while(internalNetworkStack.dataBuffer.size() < 40000){ 
        //   // Serial.print("Waiting for data...\n\r");
        // } //Wait till the buffer is at least 99% full
        Serial.print("Attempting to take checksum\n\r");
        uint32_t checkSum = byteBufferCheckSum(internalNetworkStack.dataBuffer);
        int2Bytes(checkSum, response.payload);
        #ifdef TIME_STREAMING
        int2Bytes(streamTime, response.payload + 4);
        #endif
        internalNetworkStack.queuePacket(true, response);
        internalNetworkStack.dataBuffer.resize(0); //Resizing won't reset the data at the memory locations reserved previously
        // int bufferSize = internalNetworkStack.dataBuffer.size();
        // for (int i = 0; i < bufferSize; i++){
        //   internalNetworkStack.dataBuffer.back() = 0;
        //   internalNetworkStack.dataBuffer.pop_back();
        // }
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
        
        switch ( handle_input(input_buffer, terminalParameters) ){
          case CONNECT:

            a2dpSource.set_auto_reconnect(true);
            Serial.print("Set autoreconnect... ");
            a2dpSource.start_raw("Wireless Speaker", a2dpSourceDataRetrievalNoZeroes); 
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
            Serial.printf("Buffer Size = %d, Connection Status = %d, CPU frequency = %d MHz\n\r", internalNetworkStack.dataBuffer.size(), a2dpSource.is_connected(), getCpuFrequencyMhz());
            // a2dpSource.set_auto_reconnect(true);
            // Serial.print("Playing samples with zeroes... ");
            // a2dpSource.start_raw("Wireless Speaker", a2dpSourceDataRetrievalAlt); 
            // Serial.print("Attempting to connect... ");
            break;

          case STREAM:
            a2dpSource.set_auto_reconnect(true);
            Serial.print("Direct stream starting....");
            a2dpSource.start_raw("Wireless Speaker", a2dpDirectTransfer); 
            Serial.print("Attempting to connect... ");
            // a2dpSource.set_volume(10);
            // Serial.print("Set volume...");
            Serial.print("\n\r");
            break;
          case DROP:
            Serial.print("Dropping one packet...\n\r");
            internalNetworkStack.dataBuffer.pop_front(); //get rid of one byte
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
