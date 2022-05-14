/*!
 * \file       FRTTransceiver.cpp
 * \brief      Transceiver class methods
 * \author     Xhemail Ramabaja (x.ramabaja@outlook.de)
 */

#include <FRTTransceiver.h>

//#define LOG_INFO
#define LOG_ERROR
#define LOG_WARNING

FRTTransceiver_QueueHandle FRTTransceiver_CreateQueue(FRTTransceiver_BaseType lengthOfQueue,FRTTransceiver_BaseType elementSize)
{

   if(lengthOfQueue <= 0 || lengthOfQueue > FRTTRANSCEIVER_MAXELEMENTSIZEONQUEUE)
   {
      #ifdef LOG_WARNING
      log_e("Supplied length of the queue is not valid. NULL returned [Either too small or too big]");
      #endif
      return NULL;
   }

   FRTTransceiver_QueueHandle queue = xQueueCreate(lengthOfQueue,elementSize);

   if(!queue)
   {
      #ifdef LOG_WARNING
      log_w("Queue cannot be created [Insufficient heap memory]\n");
      #endif
   }
   else
   {
      #ifdef LOG_INFO
      log_i("Queue successfully created. Queue handle returned\n");
      #endif
   }
   return queue;
}


FRTTransceiver_SemaphoreHandle FRTTransceiver_CreateSemaphore()
{
   FRTTransceiver_SemaphoreHandle semaphore = xSemaphoreCreateMutex();

   if(!semaphore)
   {
      #ifdef LOG_WARNING
      log_w("Semaphore cannot be created [Insufficient heap memory]\n");
      #endif
   }
   else
   {
      #ifdef LOG_INFO
      log_i("Semaphore successfully created. Semaphore handle returned\n");
      #endif
   }
   return semaphore;
}

bool FRTTransceiver::_checkForMessages(FRTTransceiver_QueueHandle txQueue)
{
   if(txQueue)
   {
      return uxQueueMessagesWaiting(txQueue) > 0 ? true:false;
   }

   return false;
}


int FRTTransceiver::_getAmountOfMessages(FRTTransceiver_QueueHandle queue)
{
   if(queue)
   {
      return uxQueueMessagesWaiting(queue);
   }
   return -1;
}

bool FRTTransceiver::_hasDataInterpreters()
{
   return (this->_dataAllocator && this->_dataDestroyer) ? true:false;
}

bool FRTTransceiver::_hasSemaphore(FRTTransceiver_TaskHandle partner)
{
   int pos;
   if((pos  = this->_getCommStruct(partner)) == -1)
   {
      return false;
   }

   if(this->_structCommPartners[pos].semaphore)
   {
      return true;
   }
   else
   {
      return false;
   }
}


FRTTransceiver::FRTTransceiver(uint8_t u8MaxPartners)
{
   this->_u8MaxPartners = u8MaxPartners;
   this->_structCommPartners = new struct CommunicationPartner[u8MaxPartners];
}

FRTTransceiver::~FRTTransceiver()
{
   delete[] this->_structCommPartners;
}


bool FRTTransceiver::addCommPartner(FRTTransceiver_TaskHandle partnersAddress,FRTTransceiver_SemaphoreHandle semaphore,FRTTransceiver_QueueHandle queueRX,uint8_t u8QueueLengthRx,FRTTransceiver_QueueHandle queueTX,uint8_t u8QueueLengthTx,const string partnersName)
{

   if(this->_u8CurrCommPartners + 1 > this->_u8MaxPartners)
   {
      return false;
   }

   if(partnersAddress != NULL)
   {
      this->_structCommPartners[_u8CurrCommPartners].commPartner = partnersAddress;
   }
   else
   {
      return false;
   }

   if(semaphore != NULL)
   {
      this->_structCommPartners[_u8CurrCommPartners].semaphore = semaphore;
   }
   else
   {
      return false;
   }

   if(queueRX != NULL)
   {
      this->_structCommPartners[_u8CurrCommPartners].rxQueue = queueRX;
      this->_structCommPartners[_u8CurrCommPartners].u8RxQueueLength = u8QueueLengthRx;
   }

   if(queueTX != NULL)
   {
      this->_structCommPartners[_u8CurrCommPartners].txQueue = queueTX;
      this->_structCommPartners[_u8CurrCommPartners].u8TxQueueLength = u8QueueLengthTx;
   }

   this->_structCommPartners[_u8CurrCommPartners].partnersName = partnersName;

   this->_u8CurrCommPartners++;
   return true;
}



#if defined(FRTTRANSCEIVER_32BITADDITIONALDATA)
bool FRTTransceiver::writeToQueue(FRTTransceiver_TaskHandle destination,uint8_t u8DataType,void * data,int blockTimeWrite,int blockTimeTakeSemaphore,uint32_t u32AdditionalData)
#elif defined(FRTTRANSCEIVER_64BITADDITIONALDATA)
bool FRTTransceiver::writeToQueue(FRTTransceiver_TaskHandle destination,uint8_t u8DataType,void * data,int blockTimeWrite,int blockTimeTakeSemaphore,uint64_t u64AdditionalData)
#endif
{
   int pos;

   if(!this->_hasDataInterpreters() || !this->_hasSemaphore(destination) || (pos  = this->_getCommStruct(destination)) == -1)
   {
      #ifdef LOG_WARNING
      log_e("You are not allowed to write to a queue \nOne of the following things happened:\n"
            "-[no callback functions for (allocating,freeing) data supplied]\n"
            "-[no semphores supplied]"
            "-[destination task unknown]\n");
      #endif
      return false;
   }

   if(this->_structCommPartners[pos].txQueue == NULL || _checkValidQueueLength(this->_structCommPartners[pos].u8TxQueueLength) == false || data == NULL)
   {
      #ifdef LOG_WARNING
      log_w("Action now allowed \nOne of the following things happened:\n"
            "-[no tx queue supplied]"
            "-[queue length invalid]"
            "-[data pointer null]\n");
      #endif
      return false;
   }


   unsigned long timeToWait = _checkWaitTime(blockTimeTakeSemaphore);

   if(timeToWait == -2)
   {  
      return false;
   }

   timeToWait = (timeToWait == FRTTRANSCEIVER_WAITMAX ? portMAX_DELAY : pdMS_TO_TICKS(timeToWait));

   SemaphoreHandle_t s = this->_structCommPartners[pos].semaphore;

   if(xSemaphoreTake(s,timeToWait) == pdFALSE)
   {
      #ifdef LOG_INFO
      log_i("Semaphore was not available before block time expired.")
      #endif
      return false;
   }

   timeToWait = _checkWaitTime(blockTimeWrite);

   
   uint8_t u8MessagesOnQueue;
   /* time specified is invalid || queue full */
   if(timeToWait == -2 || this->_structCommPartners[pos].u8TxQueueLength == (u8MessagesOnQueue = this->_getAmountOfMessages(this->_structCommPartners[pos].txQueue)))
   {
      xSemaphoreGive(s);
      return false;
   }

   /* space left for another element */
   this->_structCommPartners[pos].txLineContainer[u8MessagesOnQueue] =
   {
      .data = data,
      .u8DataType = u8DataType,
      #if defined(FRTTRANSCEIVER_32BITADDITIONALDATA)
      .u32AdditionalData = u32AdditionalData,
      #elif defined(FRTTRANSCEIVER_64BITADDITIONALDATA)
      .u64AdditionalData = u64AdditionalData,
      #endif
   };

   timeToWait = (timeToWait == FRTTRANSCEIVER_WAITMAX ? portMAX_DELAY : pdMS_TO_TICKS(timeToWait));

   
   FRTTransceiver_BaseType returnVal = xQueueSendToBack(this->_structCommPartners[pos].txQueue,(const void *)&this->_structCommPartners[pos].txLineContainer[u8MessagesOnQueue],(TickType_t)timeToWait);

   xSemaphoreGive(s);

   if(!(returnVal == pdTRUE))
   {
      return false;
   }

   return true;
}


bool FRTTransceiver::addCommQueue(FRTTransceiver_TaskHandle partner, FRTTransceiver_QueueHandle queueRxOrTx,uint8_t u8QueueLength,bool isTxQueue)
{
   int pos;

   if((pos = this->_getCommStruct(partner)) == -1 || queueRxOrTx == NULL)
   {
      return false;
   }

   CommunicationPartner & temp = this->_structCommPartners[pos];

   if(isTxQueue == true)
   {
      
      temp.txQueue = queueRxOrTx;
      temp.u8TxQueueLength = u8QueueLength;
   }
   else
   {
      temp.rxQueue = queueRxOrTx;
      temp.u8RxQueueLength = u8QueueLength;
   }

   return true;
}


bool FRTTransceiver::readFromQueue(FRTTransceiver_TaskHandle source,int blockTimeRead,int blockTimeTakeSemaphore)
{

   if(!this->_hasDataInterpreters() || !this->_hasSemaphore(source))
   {
      return false;
   }

   int pos;

   if((pos = this->_getCommStruct(source)) == -1 || this->_structCommPartners[pos].rxQueue == NULL)
   {
      return false;

   }

   unsigned long timeToWait = this->_checkWaitTime(blockTimeTakeSemaphore);

   if(timeToWait == -2)
   {
      return false;
   }

   timeToWait = (timeToWait == FRTTRANSCEIVER_WAITMAX ? portMAX_DELAY : pdMS_TO_TICKS(timeToWait));

   SemaphoreHandle_t s = this->_structCommPartners[pos].semaphore;

   if(xSemaphoreTake(s,timeToWait) == pdFALSE)
   {
      return false;
   }
   

   timeToWait = this->_checkWaitTime(blockTimeRead);

   if(timeToWait == -2)
   {
      xSemaphoreGive(s);
      return false;
   }

   timeToWait = (timeToWait == FRTTRANSCEIVER_WAITMAX ? portMAX_DELAY : pdMS_TO_TICKS(timeToWait));
   
   /* Here it needs to be checked whether we still have space in the tempcontainer array or not*/
   if(this->_structCommPartners[pos].rxQueueFull)
   {

      /* remove oldest data */
      this->_dataDestroyer(this->_structCommPartners[pos].tempContainer[0]);
      /* rearrange array if length at least 2 */
      if(this->_structCommPartners[pos].u8RxQueueLength - 1 > 0)
      {
         this->_rearrangeTempContainerArray(pos);
      }
      this->_structCommPartners[pos].i8CurrTempcontainerPos--;
   }
   

   FRTTransceiver_BaseType returnVal = xQueueReceive(this->_structCommPartners[pos].rxQueue,(void *)&this->_structCommPartners[pos].rxLineContainer,(TickType_t)timeToWait);

   /* errQUEUE_EMPTY returned if expression true*/
   if(!(returnVal == pdPASS))
   {
      xSemaphoreGive(s);
      return false;
   }

   this->_dataAllocator(this->_structCommPartners[pos].rxLineContainer,this->_structCommPartners[pos].tempContainer[++this->_structCommPartners[pos].i8CurrTempcontainerPos]);
   this->_structCommPartners[pos].hasBufferedData = true;

   if(this->_structCommPartners[pos].i8CurrTempcontainerPos+1 == this->_structCommPartners[pos].u8RxQueueLength)
   {
      this->_structCommPartners[pos].rxQueueFull = true;
   }
   
   xSemaphoreGive(s);
   return true;
}



void FRTTransceiver::manualDeleteAllocatedData(FRTTransceiver_TaskHandle partner)
{
   int pos;
   if(!this->_hasDataInterpreters() || (pos = this->_getCommStruct(partner)) == -1)
   {
      return;
   }

   if(this->_structCommPartners[pos].hasBufferedData)
   {
      #ifdef LOG_INFO
      log_i("Manually deleting allocated data");
      #endif
      this->_dataDestroyer(this->_structCommPartners[pos].tempContainer[0]);
      this->_structCommPartners[pos].hasBufferedData = false;
      return;
   }
}


void FRTTransceiver::manualDeleteAllAllocatedDataForLine(FRTTransceiver_TaskHandle partner)
{
   int pos;
   if(!this->_hasDataInterpreters() || (pos = this->_getCommStruct(partner) == -1))
   {
      return;
   }

   if(this->_structCommPartners[pos].hasBufferedData)
   {
      for(uint8_t u8I = 0;u8I <= this->_structCommPartners[pos].i8CurrTempcontainerPos;u8I++)
      {
         this->_dataDestroyer(this->_structCommPartners[pos].tempContainer[u8I]);
      }
      this->_structCommPartners[pos].hasBufferedData = false;
      this->_structCommPartners[pos].rxQueueFull = false;
      this->_structCommPartners[pos].i8CurrTempcontainerPos = -1;
   }
}


int FRTTransceiver::messagesOnQueue(FRTTransceiver_TaskHandle partner)
{
   int pos;

   if((pos = this->_getCommStruct(partner)) == -1)
   {
      return -1;
   }
   return this->_getAmountOfMessages(this->_structCommPartners[pos].rxQueue);
}


bool FRTTransceiver::hasDataFrom(FRTTransceiver_TaskHandle partner)
{
   int pos;

   if((pos = this->_getCommStruct(partner)) == -1)
   {
      return false;
   }

   return this->_structCommPartners[pos].hasBufferedData;
}


int FRTTransceiver::amountOfDataInAllBuffers()
{
   int amountOfDataAvail = 0;
   for(uint8_t u8I = 0;u8I < this->_u8CurrCommPartners;u8I++)
   {
      if(this->_structCommPartners[u8I].hasBufferedData){
         amountOfDataAvail += this->_structCommPartners[u8I].i8CurrTempcontainerPos + 1;
      }
   }
   return amountOfDataAvail;
}


int FRTTransceiver::_getCommStruct(FRTTransceiver_TaskHandle partner)
{
   if(!partner)
   {
      return -1;
   }

   for(int i = 0; i < this->_u8CurrCommPartners;i++)
   {
      if(this->_structCommPartners[i].commPartner == partner)
      {
         return i;
      }
   }
   return -1;
}



/* get the tail of the buffer*/
const TempDataContainer * FRTTransceiver::getNewestBufferedDataFrom(FRTTransceiver_TaskHandle partner)
{
   int pos = this->_getCommStruct(partner);

   if(pos == -1)
   {
      return NULL;
   }

   if(this->_structCommPartners[pos].hasBufferedData)
   {
      return (const TempDataContainer *)&this->_structCommPartners[pos].tempContainer[this->_structCommPartners[pos].i8CurrTempcontainerPos];
   }
   return NULL;
}

/* get the head of the buffer */
const TempDataContainer * FRTTransceiver::getOldestBufferedDataFrom(FRTTransceiver_TaskHandle partner)
{
   int pos = this->_getCommStruct(partner);

   if(pos == -1)
   {
      return NULL;
   }

   if(this->_structCommPartners[pos].hasBufferedData)
   {
      return (const TempDataContainer *)&this->_structCommPartners[pos].tempContainer[0];
   }
   return NULL;
}

const TempDataContainer * FRTTransceiver::getBufferedDataFrom(FRTTransceiver_TaskHandle partner, uint8_t u8PositionInBuffer)
{
   int pos = this->_getCommStruct(partner);
   
   if(pos == -1)
   {
      return NULL;
   }

   if(this->_structCommPartners[pos].hasBufferedData && u8PositionInBuffer >= 0 && u8PositionInBuffer <= this->_structCommPartners[pos].i8CurrTempcontainerPos)
   {
      return (const TempDataContainer *)&this->_structCommPartners[pos].tempContainer[u8PositionInBuffer];
   }
   
   return NULL;
}


void FRTTransceiver::addDataAllocateCallback(void(*funcPointerCallback)(const DataContainerOnQueue &,TempDataContainer &))
{
   this->_dataAllocator = funcPointerCallback;
}


void FRTTransceiver::addDataFreeCallback(void (*funcPointerCallback)(TempDataContainer &))
{
   this->_dataDestroyer = funcPointerCallback;
}


string FRTTransceiver::getPartnersName(FRTTransceiver_TaskHandle partner)
{
   int pos = this->_getCommStruct(partner);

   if(pos == -1)
   {
      return NULL;
   }

   return this->_structCommPartners[pos].partnersName;
}


int FRTTransceiver::_checkWaitTime(int timeMs)
{
   /* WAITMAX is defined as -1, If timeMs element of [-infinite;0[, than display error! */
   if(timeMs < FRTTRANSCEIVER_WAITMAX || (timeMs > FRTTRANSCEIVER_WAITMAX && timeMs < 0))
   {
      #ifdef LOG_WARNING
      log_w("Nothing sent [invalid wait time specified]\n");
      #endif
      return -2;
   }
   return timeMs;
}

bool FRTTransceiver::_checkValidQueueLength(uint8_t u8QueueLength)
{
   return !(u8QueueLength <= 0 || u8QueueLength > FRTTRANSCEIVER_MAXELEMENTSIZEONQUEUE);
}

void FRTTransceiver::_rearrangeTempContainerArray(uint8_t u8CommStructPos)
{
   for(uint8_t u8I = 1; u8I <= this->_structCommPartners[u8CommStructPos].i8CurrTempcontainerPos;u8I++)
   {
      this->_structCommPartners[u8CommStructPos].tempContainer[u8I-1] = this->_structCommPartners[u8CommStructPos].tempContainer[u8I];
   }
}