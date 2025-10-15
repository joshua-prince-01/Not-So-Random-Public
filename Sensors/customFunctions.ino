/*
  This function needs to run inside the setup() function of your main .ino file.
  This will find the device's mac address and send it to the cloud for reading on a  dashboard. This is critical for debugging network connectivity issues per device.
  */

void getArduinoMac(String& cloudMacVar, char globalMac[18]) {
  // cloudMacVar is your cloud variable
  // globalMac is your global variable used by the arduino locally
  
  // Get MAC address and store to local variable, macRaw
  byte macRaw[6];
  WiFi.macAddress(macRaw);
 
  // Format MAC address as a string XX:XX:XX:XX:XX:XX
  sprintf(globalMac, "%02X:%02X:%02X:%02X:%02X:%02X", macRaw[5], macRaw[4], macRaw[3],    macRaw[2], macRaw[1], macRaw[0]);
 
  // Send to cloud
  cloudMacVar = globalMac;
}