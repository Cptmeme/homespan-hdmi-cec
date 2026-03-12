#include "HomeSpan.h"
#include "CEC_Device.h"

#define CEC_GPIO 4
#define CEC_DEVICE_TYPE CEC_Device::CDT_PLAYBACK_DEVICE
#define CEC_PHYSICAL_ADDRESS 0x1000

class HomeSpanTV;

// implement application specific CEC device
class MyCEC_Device : public CEC_Device
{
  HomeSpanTV* connectedTV;
  public:
    void SetTVDevice(HomeSpanTV* tv);
  protected:
    virtual bool LineState();
    virtual void SetLineState(bool);
    virtual void OnReady(int logicalAddress);
    virtual void OnReceiveComplete(unsigned char* buffer, int count, bool ack);
    virtual void OnTransmitComplete(unsigned char* buffer, int count, bool ack);
};

bool MyCEC_Device::LineState()
{
  int state = digitalRead(CEC_GPIO);
  return state != LOW;
}

void MyCEC_Device::SetLineState(bool state)
{
  if (state) {
    pinMode(CEC_GPIO, INPUT_PULLUP);
  } else {
    digitalWrite(CEC_GPIO, LOW);
    pinMode(CEC_GPIO, OUTPUT);
  }
  // give enough time for the line to settle before sampling it
  delayMicroseconds(50);
}

void MyCEC_Device::OnReady(int logicalAddress)
{
  // This is called after the logical address has been allocated

  unsigned char buf[4] = {0x84, CEC_PHYSICAL_ADDRESS >> 8, CEC_PHYSICAL_ADDRESS & 0xff, CEC_DEVICE_TYPE};

  DbgPrint("Device ready, Logical address assigned: %d\n", logicalAddress);

  TransmitFrame(0xf, buf, 4); // <Report Physical Address>
}

void MyCEC_Device::OnTransmitComplete(unsigned char* buffer, int count, bool ack)
{
  // This is called after a frame is transmitted.
  DbgPrint("Packet sent at %ld: %02X", millis(), buffer[0]);
  for (int i = 1; i < count; i++)
    DbgPrint(":%02X", buffer[i]);
  if (!ack)
    DbgPrint(" NAK");
  DbgPrint("\n");
}

MyCEC_Device device;

struct HomeSpanTVSpeaker : Service::TelevisionSpeaker {
    SpanCharacteristic *volume = new Characteristic::VolumeSelector();
    SpanCharacteristic *volumeType = new Characteristic::VolumeControlType(3);

    HomeSpanTVSpeaker(const char *name) : Service::TelevisionSpeaker() {
      new Characteristic::ConfiguredName(name);
      Serial.printf("Configured Speaker: %s\n", name);
    }

    boolean update() override {
      if (volume->updated()) {
        if (volume->getNewVal() == 0) {
          Serial.printf("Volume Up");
          device.TransmitFrame(0x40, (unsigned char*)"\x46", 1);
        } else {
          Serial.printf("Volume Down");
          device.TransmitFrame(0x40, (unsigned char*)"\x8f", 1);
        }
      }

      if (volumeType->updated()) {
        Serial.printf("Updated volume type\n");
      }

      return (true);
    }
};

struct HomeSpanTV : Service::Television {

  SpanCharacteristic *active = new Characteristic::Active(0);                     
  SpanCharacteristic *activeID = new Characteristic::ActiveIdentifier(3);         
  SpanCharacteristic *remoteKey = new Characteristic::RemoteKey();                
  SpanCharacteristic *settingsKey = new Characteristic::PowerModeSelection();     

  // NEW: Command Queue Variables
  int pendingInput = 0;    
  uint32_t lastInteractionTime = 0;

  HomeSpanTV(const char *name) : Service::Television() {
    new Characteristic::ConfiguredName(name);             
  }

  boolean update() override {

    // --- 1. POWER CONTROL ---
    if (active->updated()) {
      Serial.printf("Set TV Power to: %s\n", active->getNewVal() ? "ON" : "OFF");
      if (active->getNewVal()) device.TransmitFrame(0x0, (unsigned char*)"\x04", 1); 
      else device.TransmitFrame(0x0, (unsigned char*)"\x36", 1); 
      lastInteractionTime = millis(); // Reset bus timer
    }

    // --- 2. INPUT SWITCHING (Queue it up!) ---
    if (activeID->updated()) {
      pendingInput = activeID->getNewVal();
      Serial.printf("HomeKit requests HDMI %d. Queuing to prevent bus crash...\n", pendingInput);
      lastInteractionTime = millis(); // Resets the 800ms countdown every time you tap
    }

    // --- 3. REMOTE CONTROL WIDGET ---
    if (remoteKey->updated()) {
      unsigned char pressCmd[2] = {0x44, 0x00};
      bool validKey = true;
      switch (remoteKey->getNewVal()) {
        case 4: pressCmd[1] = 0x01; break; case 5: pressCmd[1] = 0x02; break; 
        case 6: pressCmd[1] = 0x03; break; case 7: pressCmd[1] = 0x04; break; 
        case 8: pressCmd[1] = 0x00; break; case 9: pressCmd[1] = 0x0D; break; 
        case 11: pressCmd[1]= 0x44; break; case 15: pressCmd[1]= 0x35; break; 
        default: validKey = false;
      }
      if (validKey) {
        device.TransmitFrame(0x0, pressCmd, 2); 
        delay(50); 
        device.TransmitFrame(0x0, (unsigned char*)"\x45", 1); 
        lastInteractionTime = millis(); // Reset bus timer
      }
    }

    return (true);
  }

  // --- BACKGROUND QUEUE EXECUTOR ---
  void loop() override {
    if (pendingInput > 0 && (millis() - lastInteractionTime > 800)) {
      
      Serial.printf("Executing clean switch to HDMI %d\n", pendingInput);
      
      if (pendingInput == 1) {
        device.TransmitFrame(0x0F, (unsigned char*)"\x82\x10\x00", 3); 
        device.TransmitFrame(0x0F, (unsigned char*)"\x86\x10\x00", 3); 
      } else if (pendingInput == 2) {
        device.TransmitFrame(0x0F, (unsigned char*)"\x82\x20\x00", 3);
        device.TransmitFrame(0x0F, (unsigned char*)"\x86\x20\x00", 3);
      } else if (pendingInput == 3) {
        device.TransmitFrame(0x0F, (unsigned char*)"\x82\x30\x00", 3);
        device.TransmitFrame(0x0F, (unsigned char*)"\x86\x30\x00", 3);
      }
      
      pendingInput = 0; // Clear the queue!
      lastInteractionTime = millis();
    }
  }
};
void MyCEC_Device::SetTVDevice(HomeSpanTV* tv) {
  connectedTV = tv;
  //tv::active->setVal(true);
};

void MyCEC_Device::OnReceiveComplete(unsigned char* buffer, int count, bool ack)
{
  if (count < 1) return;

  // 1. Physical Remote: TV turned off
  if (buffer[0] == 0x0F && buffer[1] == 0x36) {
    if (connectedTV->active->getVal() != 0) {
      connectedTV->active->setVal(0); 
      Serial.println("Sync: TV turned OFF via remote");
    }
  }

  // 2. Physical Remote: Input changed
  int physicalAddress = -1;
  if (buffer[1] == 0x82 && count >= 4) physicalAddress = (buffer[2] << 8) | buffer[3];
  else if (buffer[1] == 0x80 && count >= 6) physicalAddress = (buffer[4] << 8) | buffer[5];
  else if (buffer[1] == 0x86 && count >= 4) physicalAddress = (buffer[2] << 8) | buffer[3];

  if (physicalAddress != -1) {
    int reportedInput = 0;
    if (physicalAddress == 0x1000) reportedInput = 1;
    else if (physicalAddress == 0x2000) reportedInput = 2;
    else if (physicalAddress == 0x3000) reportedInput = 3;

    // Only sync if the change didn't come from our own HomeKit queue
    if (reportedInput > 0 && connectedTV->pendingInput == 0 && connectedTV->activeID->getVal() != reportedInput) {
      connectedTV->activeID->setVal(reportedInput); 
      if (connectedTV->active->getVal() == 0) connectedTV->active->setVal(1);
      Serial.printf("Sync: TV updated to HDMI %d\n", reportedInput);
    }
  }

  if ((buffer[0] & 0xf) != LogicalAddress() && (buffer[0] & 0xf) != 0xF) return; 

  switch (buffer[1]) {
    case 0x83: { 
        unsigned char buf[4] = {0x84, CEC_PHYSICAL_ADDRESS >> 8, CEC_PHYSICAL_ADDRESS & 0xff, CEC_DEVICE_TYPE};
        TransmitFrame(0xf, buf, 4); 
        break;
      }
    case 0x8c: TransmitFrame(0xf, (unsigned char*)"\x87\x01\x23\x45", 4); break;
    case 0x46: TransmitFrame(0x4f, (unsigned char*)"\x47\x52\x44\x4d", 4); break;
  }
}


void setup() {
  gpio_reset_pin(GPIO_NUM_4);

  Serial.begin(115200);

  homeSpan.enableOTA();
  homeSpan.begin(Category::Television, "HomeSpan Television");

  SPAN_ACCESSORY();

// --- HDMI 1 ---
  SpanService *hdmi1 = new Service::InputSource();    
  new Characteristic::ConfiguredName("HDMI 1");
  new Characteristic::InputSourceType(3);           // 3 = HDMI Type (Required by iOS!)
  new Characteristic::IsConfigured(1);              // 1 = Configured
  new Characteristic::CurrentVisibilityState(0);    // 0 = Shown in the selection menu
  new Characteristic::Identifier(1);

  // --- HDMI 2 ---
  SpanService *hdmi2 = new Service::InputSource();
  new Characteristic::ConfiguredName("HDMI 2");
  new Characteristic::InputSourceType(3);
  new Characteristic::IsConfigured(1);              
  new Characteristic::CurrentVisibilityState(0);    
  new Characteristic::Identifier(2);

  // --- HDMI 3 ---
  SpanService *hdmi3 = new Service::InputSource();
  new Characteristic::ConfiguredName("HDMI 3");
  new Characteristic::InputSourceType(3);
  new Characteristic::IsConfigured(1);              
  new Characteristic::CurrentVisibilityState(0);    
  new Characteristic::Identifier(3);

  SpanService *speaker = new HomeSpanTVSpeaker("My Speaker");

  HomeSpanTV* tv = (new HomeSpanTV("Samsung TV"));
  tv->addLink(hdmi1)
  ->addLink(hdmi2)
  ->addLink(hdmi3)
  ->addLink(speaker)
  ;

  device.SetTVDevice(tv);

  pinMode(CEC_GPIO, INPUT_PULLUP);
  device.Initialize(CEC_PHYSICAL_ADDRESS, CEC_DEVICE_TYPE, true); // Promiscuous mode
  homeSpan.autoPoll();
}

void loop() {
  //homeSpan.poll();
  device.Run();
}
