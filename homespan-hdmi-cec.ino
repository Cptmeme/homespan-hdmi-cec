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

  SpanCharacteristic *active = new Characteristic::Active(0);                     // TV On/Off 
  SpanCharacteristic *activeID = new Characteristic::ActiveIdentifier(3);         // HDMI Port
  SpanCharacteristic *remoteKey = new Characteristic::RemoteKey();                // Remote Widget
  SpanCharacteristic *settingsKey = new Characteristic::PowerModeSelection();     

  HomeSpanTV(const char *name) : Service::Television() {
    new Characteristic::ConfiguredName(name);             
    Serial.printf("Configured TV: %s\n", name);
  }

boolean update() override {

    // --- 1. POWER CONTROL ---
    if (active->updated()) {
      Serial.printf("Set TV Power to: %s\n", active->getNewVal() ? "ON" : "OFF");
      if (active->getNewVal()) {
        // Send directly to TV (0x0)
        device.TransmitFrame(0x0, (unsigned char*)"\x04", 1); 
      } else {
        // Send directly to TV (0x0) - Samsung ignores broadcasts for Standby!
        device.TransmitFrame(0x0, (unsigned char*)"\x36", 1); 
      }
    }

    // --- 2. INPUT SWITCHING ---
    if (activeID->updated()) {
      int input = activeID->getNewVal();
      Serial.printf("Switching Input to: HDMI %d\n", input);
      
      // The One-Two Punch: 
      // 1. Force TV to switch (Active Source 0x82)
      // 2. Wake the connected device (Set Stream Path 0x86)
      if (input == 1) {
        device.TransmitFrame(0x0F, (unsigned char*)"\x82\x10\x00", 3); 
        device.TransmitFrame(0x0F, (unsigned char*)"\x86\x10\x00", 3); 
      }
      else if (input == 2) {
        device.TransmitFrame(0x0F, (unsigned char*)"\x82\x20\x00", 3);
        device.TransmitFrame(0x0F, (unsigned char*)"\x86\x20\x00", 3);
      }
      else if (input == 3) {
        device.TransmitFrame(0x0F, (unsigned char*)"\x82\x30\x00", 3);
        device.TransmitFrame(0x0F, (unsigned char*)"\x86\x30\x00", 3);
      }
    }

    // --- 3. REMOTE CONTROL WIDGET ---
    if (remoteKey->updated()) {
      Serial.printf("Remote key: %d\n", remoteKey->getNewVal());
      
      unsigned char pressCmd[2] = {0x44, 0x00};
      bool validKey = true;

      switch (remoteKey->getNewVal()) {
        case 4: pressCmd[1] = 0x01; break; // UP
        case 5: pressCmd[1] = 0x02; break; // DOWN
        case 6: pressCmd[1] = 0x03; break; // LEFT
        case 7: pressCmd[1] = 0x04; break; // RIGHT
        case 8: pressCmd[1] = 0x00; break; // SELECT
        case 9: pressCmd[1] = 0x0D; break; // BACK
        case 11: pressCmd[1]= 0x44; break; // PLAY
        case 15: pressCmd[1]= 0x35; break; // INFO
        default: validKey = false;
      }

      if (validKey) {
        // Send directly to TV (0x0)
        device.TransmitFrame(0x0, pressCmd, 2); 
        delay(50); 
        device.TransmitFrame(0x0, (unsigned char*)"\x45", 1); // Release key
      }
    }

    return (true);
  }
};
void MyCEC_Device::SetTVDevice(HomeSpanTV* tv) {
  connectedTV = tv;
  //tv::active->setVal(true);
};

void MyCEC_Device::OnReceiveComplete(unsigned char* buffer, int count, bool ack)
{
  // --- RAW EAVESDROPPING LOG ---
  Serial.printf("Raw RX: %02X", buffer[0]);
  for (int i = 1; i < count; i++) {
    Serial.printf(":%02X", buffer[i]);
  }
  Serial.println(ack ? "" : " NAK");

  if (count < 1) return;

  // --- TWO-WAY SYNC LOGIC ---
  
  // 1. Listen for Standby (0x36) broadcast
  if (buffer[0] == 0x0F && buffer[1] == 0x36) {
    if (connectedTV->active->getVal() != 0) {
      connectedTV->active->setVal(0); 
      Serial.println("Sync: TV turned OFF via remote");
    }
  }

  // 2. Listen for Active Source (0x82) broadcast
  if (buffer[1] == 0x82 && count >= 4) {
    int physicalAddress = (buffer[2] << 8) | buffer[3];
    if (physicalAddress == 0x1000) connectedTV->activeID->setVal(1);
    else if (physicalAddress == 0x2000) connectedTV->activeID->setVal(2);
    else if (physicalAddress == 0x3000) connectedTV->activeID->setVal(3);
    
    if (connectedTV->active->getVal() == 0) {
      connectedTV->active->setVal(1); 
    }
    Serial.printf("Sync: Input changed to %04X\n", physicalAddress);
  }

  // 3. Listen for Routing Change (0x80) broadcast
  if (buffer[1] == 0x80 && count >= 6) { 
    int newAddress = (buffer[4] << 8) | buffer[5];
    if (newAddress == 0x1000) connectedTV->activeID->setVal(1);
    else if (newAddress == 0x2000) connectedTV->activeID->setVal(2);
    else if (newAddress == 0x3000) connectedTV->activeID->setVal(3);
    
    if (connectedTV->active->getVal() == 0) {
      connectedTV->active->setVal(1);
    }
    Serial.printf("Sync: TV routed to %04X\n", newAddress);
  }
  // 4. Listen for Set Stream Path (0x86) broadcast from the TV
  if (buffer[1] == 0x86 && count >= 4) {
    int targetAddress = (buffer[2] << 8) | buffer[3];
    if (targetAddress == 0x1000) connectedTV->activeID->setVal(1);
    else if (targetAddress == 0x2000) connectedTV->activeID->setVal(2);
    else if (targetAddress == 0x3000) connectedTV->activeID->setVal(3);
    
    if (connectedTV->active->getVal() == 0) {
      connectedTV->active->setVal(1); // Ensure TV shows as ON
    }
    Serial.printf("Sync: TV set stream path to %04X\n", targetAddress);
  }

  // Ignore messages not explicitly sent to us (so we don't reply to them)
  if ((buffer[0] & 0xf) != LogicalAddress() && (buffer[0] & 0xf) != 0xF)
    return; 

  // --- ORIGINAL HANDLERS ---
  switch (buffer[1]) {
    case 0x83: { // <Give Physical Address>
        unsigned char buf[4] = {0x84, CEC_PHYSICAL_ADDRESS >> 8, CEC_PHYSICAL_ADDRESS & 0xff, CEC_DEVICE_TYPE};
        TransmitFrame(0xf, buf, 4); 
        break;
      }
    case 0x8c: // <Give Device Vendor ID>
      TransmitFrame(0xf, (unsigned char*)"\x87\x01\x23\x45", 4); 
      break;
    case 0x46: // Give OSD Name
      TransmitFrame(0x4f, (unsigned char*)"\x47\x52\x44\x4d", 4); 
      break;
  }
}

///////////////////////////////

void setup() {
  gpio_reset_pin(GPIO_NUM_4);

  Serial.begin(115200);

  homeSpan.enableOTA();
  homeSpan.begin(Category::Television, "HomeSpan Television");

  SPAN_ACCESSORY();

  // Below we define 10 different InputSource Services using different combinations
  // of Characteristics to demonstrate how they interact and appear to the user in the Home App

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

  // --- SPEAKER ---
  // Note: I removed the extra VolumeSelector() and VolumeControlType() that were here, 
  // because they are already created inside the HomeSpanTVSpeaker struct!
  SpanService *speaker = new HomeSpanTVSpeaker("My Speaker");

  HomeSpanTV* tv = (new HomeSpanTV("Samsung TV"));                         // Define a Television Service.  Must link in InputSources!
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

///////////////////////////////

void loop() {
  //homeSpan.poll();
  device.Run();
}
