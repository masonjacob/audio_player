#include "Arduino.h"
#include "DFRobotDFPlayerMini.h"
#include "EasyButton.h"
#include <ctype.h>
#include <FlashStorage_SAMD.h>

#define DEFAULT_VOLUME 20  // Default volume if EEPROM is empty

// FlashStorage_SAMD object to hold a single uint8_t volume value
FlashStorage(volumeFlash, uint8_t);

// Board-specific serial port configurations
#ifdef BOARD_SEEED_XIAO
// XIAO uses Serial for USB communication and Serial1 for DFPlayer
#define USBSerial Serial // USB serial for debug output
#define FPSerial Serial1 // Hardware serial for DFPlayer

#define BUTTON_1_PIN 10
#define BUTTON_2_PIN 2
#define BUTTON_3_PIN 3

#elif defined(BOARD_NANO)
// Nano uses Serial for USB and SoftwareSerial for DFPlayer
#include <SoftwareSerial.h>
#define USBSerial Serial  // USB serial for debug output
SoftwareSerial DFSerial(19, 18); // RX, TX pins for DFPlayer
#define FPSerial DFSerial // Software serial for DFPlayer
#define BUTTON_1_PIN 2
#define BUTTON_2_PIN 3
#define BUTTON_3_PIN 4
#else
// Default configuration (assumes XIAO-like setup)
#define USBSerial Serial
#define FPSerial Serial1 // Hardware serial for DFPlayer (same as XIAO)
#define BUTTON_1_PIN 2
#define BUTTON_2_PIN 3
#define BUTTON_3_PIN 4
#endif

// Simple track mapping structure
struct TrackMapping
{
  const char *name; // For named sounds (NULL for indexed tracks)
  uint8_t track;    // Track number to play
};

// Maximum number of tracks in each folder
#define NUM_UI_FILES 9      // Number of files in UI folder (01)
#define NUM_VOICE_FILES 1   // Number of files in Voice folder (02)
#define NUM_MUSIC_FILES 4   // Number of files in Music folder (03)
#define NUM_CANDIDS_FILES 1 // Number of files in Candids folder (04)

#define BAUDRATE 115200

enum Buttons
{
  TopRight,
  BottomRight,
  BottomLeft,
};

enum Folders
{
  UI = 1,     // Folder 01: UI sounds, feedback tones
  Voice = 2,  // Folder 02: Voice clips
  Music = 3,  // Folder 03: Music tracks
  Candids = 4 // Folder 04: Candid recordings
};

// Sound effects array (UI sounds, named tracks)
const TrackMapping SOUNDS[NUM_UI_FILES] = {
    // UI Sounds
    {"startup", 1},
    {"tone1", 2},
    {"tone2", 3},
    // Mode Change Voice Indicator
    {"music_mode", 4},
    {"voice_mode", 5},
    {"candids_mode", 6},
    {"settings_mode", 7},
    {"tone3", 8}, // Placeholder for future use
    {"favorites_mode", 9}};

enum Mode
{
  MODE_FAVORITES,
  MODE_VOICE,
  MODE_MUSIC,
  MODE_CANDIDS,
  MODE_SETTINGS
};

Mode currentMode = MODE_FAVORITES;
Mode previousMode = MODE_FAVORITES; // used when entering/exiting config

int lastPlayedTrack = -1; // last DFPlayer.play() track number
bool isPlaying = false;

// Favorites mapping: one clip per physical button when in MODE_FAVORITES.
// Assumption: map to the first three tracks in the Music folder by default.
struct FavoriteMapping
{
  uint8_t folder;
  uint8_t track;
};

const FavoriteMapping FAVORITES[3] = {
    {Music, 1},
    {Music, 2},
    {Music, 3}};

DFRobotDFPlayerMini DFPlayer;

EasyButton button1(BUTTON_1_PIN);
EasyButton button2(BUTTON_2_PIN);
EasyButton button3(BUTTON_3_PIN);

void printDetail(uint8_t type, int value);
void handleSerialCommands();
int findSoundTrack(const char *name);
int getTrackFromArray(const TrackMapping *array, int maxSize, int index);
void playFolderTrack(uint8_t folder, uint8_t track);
void playUISound(const char *name);
void playRandomFromFolder(uint8_t folder, uint8_t maxTracks);
void enterSettingsMode();
void exitSettingsMode();
void playRandomTrack();
void changePlaybackMode();
void replayLastTrack();
void playFavorite(int idx);
void togglePlayPause();
void toggleSettingsMode();
void increaseVolume();
void decreaseVolume();
void button1ISR();
void button1Pressed();
void button1longPressed();
void button2ISR();
void button2Pressed();
void button3ISR();
void button3Pressed();
void button3longPressed();
void saveVolumeToEEPROM(uint8_t volume);
uint8_t loadVolumeFromEEPROM();

void setup()
{
  // Initialize USB serial for debugging
  USBSerial.begin(USB_SERIAL_BAUD);
  // USBSerial.println(F("Initializing..."));

  FPSerial.begin(FP_SERIAL_BAUD); // Hardware serial for DFPlayer

  // USBSerial.println(F("Serial ports initialized"));
  // Serial.println(F("DFRobot DFPlayer Mini Demo"));
  // Serial.println(F("Initializing DFPlayer ... (May take 3~5 seconds)"));

  if (!DFPlayer.begin(FPSerial, /*isACK = */ true, /*doReset = */ true))
  { // Use serial to communicate with mp3.
    USBSerial.println(F("Unable to begin:"));
    while (true)
      ;
  }
  USBSerial.println(F("DFPlayer Mini online."));

  DFPlayer.setTimeOut(1000); // Set serial communictaion time out 500ms

  //----Set volume from EEPROM----
  uint8_t savedVolume = loadVolumeFromEEPROM();
  DFPlayer.volume(savedVolume); // Set volume value (0~30)

  DFPlayer.EQ(DFPLAYER_EQ_NORMAL);

  DFPlayer.outputDevice(DFPLAYER_DEVICE_SD);

  //  DFPlayer.sleep();     //sleep
  //  DFPlayer.reset();     //Reset the module
  //  DFPlayer.enableDAC();  //Enable On-chip DAC
  //  DFPlayer.disableDAC();  //Disable On-chip DAC
  //  DFPlayer.outputSetting(true, 15); //output setting, enable the output and set the gain to 15

  // Initialize the buttons
  button1.begin();
  button1.onPressed(button1Pressed);
  button1.onPressedFor(1000, button1longPressed);
  // button1.onSequence(2, 500, playRandomTrack); // Double click with 500ms timeout

  button2.begin();
  button2.onPressed(button2Pressed);
  // button2.onPressedFor(1000, playRandomTrack);
  // button2.onSequence(2, 500, replayLastTrack); // Double click with 500ms timeout

  button3.begin();
  button3.onPressed(button3Pressed);
  button3.onPressedFor(1000, button3longPressed);

  // Play startup sound from UI folder
  playUISound("startup");
  delay(5000);
  playUISound("favorites_mode");
  delay(100);
}

void loop()
{
  button1.read();
  button2.read();
  button3.read();
}

void printDetail(uint8_t type, int value)
{
  switch (type)
  {
  case TimeOut:
    // Serial.println(F("Time Out!"));
    break;
  case WrongStack:
    // Serial.println(F("Stack Wrong!"));
    break;
  case DFPlayerCardInserted:
    // Serial.println(F("Card Inserted!"));
    break;
  case DFPlayerCardRemoved:
    // Serial.println(F("Card Removed!"));
    break;
  case DFPlayerCardOnline:
    // Serial.println(F("Card Online!"));
    break;
  case DFPlayerUSBInserted:
    // Serial.println("USB Inserted!");
    break;
  case DFPlayerUSBRemoved:
    // Serial.println("USB Removed!");
    break;
  case DFPlayerPlayFinished:
    Serial.print(F("Number:"));
    Serial.print(value);
    // Serial.println(F(" Play Finished!"));
    break;
  case DFPlayerError:
    Serial.print(F("DFPlayerError:"));
    switch (value)
    {
    case Busy:
      // Serial.println(F("Card not found"));
      break;
    case Sleeping:
      // Serial.println(F("Sleeping"));
      break;
    case SerialWrongStack:
      // Serial.println(F("Get Wrong Stack"));
      break;
    case CheckSumNotMatch:
      // Serial.println(F("Check Sum Not Match"));
      break;
    case FileIndexOut:
      // Serial.println(F("File Index Out of Bound"));
      break;
    case FileMismatch:
      // Serial.println(F("Cannot Find File"));
      break;
    case Advertise:
      // Serial.println(F("In Advertise"));
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }
}

// -- Function implementations moved here --

int findSoundTrack(const char *name)
{
  for (int i = 0; i < NUM_UI_FILES; i++)
  {
    if (SOUNDS[i].name && strcmp(SOUNDS[i].name, name) == 0)
    {
      return SOUNDS[i].track;
    }
  }
  return -1;
}

int getTrackFromArray(const TrackMapping *array, int maxSize, int index)
{
  if (index >= 0 && index < maxSize)
  {
    return array[index].track;
  }
  return -1;
}

// Play the favorite indexed by button (0..2). Safe no-op if mapping invalid.
void playFavorite(int idx)
{
  if (idx < 0 || idx >= 3)
    return;
  uint8_t folder = FAVORITES[idx].folder;
  uint8_t track = FAVORITES[idx].track;
  if (folder > 0 && track > 0)
  {
    playFolderTrack(folder, track);
  }
}

// Play a track from a specific folder
void playFolderTrack(uint8_t folder, uint8_t track)
{
  if (folder <= 0 || track <= 0)
    return;
  DFPlayer.playFolder(folder, track);
  lastPlayedTrack = track;
  isPlaying = true;
  USBSerial.print(F("Playing folder "));
  USBSerial.print(folder);
  USBSerial.print(F(" track "));
  USBSerial.println(track);
}

// Helper: play a track from UI sounds folder
void playUISound(const char *name)
{
  if (!name)
    return;
  for (int i = 0; i < NUM_UI_FILES; i++)
  {
    if (SOUNDS[i].name && strcmp(SOUNDS[i].name, name) == 0)
    {
      playFolderTrack(UI, SOUNDS[i].track);
      return;
    }
  }
}

// Helper: play random track from a folder
void playRandomFromFolder(uint8_t folder, uint8_t maxTracks)
{
  if (folder <= 0 || maxTracks <= 0)
    return;
  uint8_t track = random(1, maxTracks + 1);
  playFolderTrack(folder, track);
}

void enterSettingsMode()
{
  previousMode = currentMode;
  currentMode = MODE_SETTINGS;
  // Serial.println(F("Entered SETTINGS mode"));
  playUISound("settings_mode");
}

void exitSettingsMode()
{
  currentMode = previousMode;
  // Serial.println(F("Exited SETTINGS mode"));
  // play menu close sound if defined
  switch (currentMode)
  {
  case MODE_FAVORITES:
    playUISound("favorites_mode");
    break;
  case MODE_VOICE:
    playUISound("voice_mode");
    break;
  case MODE_MUSIC:
    playUISound("music_mode");
    break;
  case MODE_CANDIDS:
    playUISound("candids_mode");
    break;
  default:
    break;
  }
}

void playRandomTrack()
{
  switch (currentMode)
  {
  case MODE_VOICE:
    playRandomFromFolder(Voice, NUM_VOICE_FILES);
    break;
  case MODE_MUSIC:
    playRandomFromFolder(Music, NUM_MUSIC_FILES);
    break;
  case MODE_CANDIDS:
    playRandomFromFolder(Candids, NUM_CANDIDS_FILES);
    break;
  default:
    break;
  }
}

void changePlaybackMode()
{
  USBSerial.println("Button 1 long pressed");
  // Toggle between modes on long press
  switch (currentMode)
  {
  case MODE_FAVORITES:
    currentMode = MODE_VOICE;
    // Serial.println(F("Switched to VOICE mode"));
    playUISound("voice_mode");
    break;
  case MODE_VOICE:
    currentMode = MODE_MUSIC;
    // Serial.println(F("Switched to MUSIC mode"));
    playUISound("music_mode");
    break;
  case MODE_MUSIC:
    currentMode = MODE_CANDIDS;
    // Serial.println(F("Switched to CANDIDS mode"));
    playUISound("candids_mode");
    break;
  case MODE_CANDIDS:
    currentMode = MODE_FAVORITES;
    // Serial.println(F("Switched to FAVORITES mode"));
    playUISound("favorites_mode");
    break;
  default:
    break;
  }
}

void replayLastTrack()
{
  if (currentMode != MODE_SETTINGS)
  {
    if (lastPlayedTrack > 0)
    {
      uint8_t currentFolder = Voice; // default to voice folder
      switch (currentMode)
      {
      case MODE_MUSIC:
        currentFolder = Music;
        break;
      case MODE_CANDIDS:
        currentFolder = Candids;
        break;
      default:
        currentFolder = Voice;
        break;
      }
      playFolderTrack(currentFolder, lastPlayedTrack);
    }
    else
    {
      // USBSerial.println(F("No last track to replay"));
    }
  }
}

void togglePlayPause()
{
  if (currentMode == MODE_SETTINGS)
    return;

  if (isPlaying)
  {
    DFPlayer.pause();
    isPlaying = false;
    USBSerial.println(F("Paused"));
  }
  else
  {
    if (lastPlayedTrack > 0)
    {
      DFPlayer.start();
      isPlaying = true;
      // USBSerial.println(F("Resumed"));
    }
    else
    {
      // USBSerial.println(F("No track to resume"));
    }
  }
}

void toggleSettingsMode()
{
  // Toggle configuration mode
  if (currentMode != MODE_SETTINGS)
  {
    enterSettingsMode();
  }
  else
  {
    // Save configuration (placeholder) and exit
    // If persistent storage needed, write to EEPROM here.
    // Serial.println(F("Saving configuration and exiting settings mode"));
    exitSettingsMode();
  }
}

void increaseVolume()
{
  uint8_t currentVolume = loadVolumeFromEEPROM();
  if (currentVolume < 30) {
    currentVolume++;
    DFPlayer.volume(currentVolume);
    saveVolumeToEEPROM(currentVolume);
    playFolderTrack(UI, 8); // Play tone1 as feedback
  }
}

void decreaseVolume()
{
  uint8_t currentVolume = loadVolumeFromEEPROM();
  if (currentVolume > 0) {
    currentVolume--;
    DFPlayer.volume(currentVolume);
    saveVolumeToEEPROM(currentVolume);
    playFolderTrack(UI, 8); // Play tone2 as feedback
  }
}

void button1ISR()
{
  button1.read();
}

void button1Pressed()
{
  if (currentMode == MODE_SETTINGS)
  {
    increaseVolume();
    return;
  }

  if (currentMode == MODE_FAVORITES)
  {
    playFavorite(0);
    return;
  }

  // Default behavior for other non-settings modes
  playRandomTrack();
}

void button1longPressed()
{
  changePlaybackMode();
}

void button2ISR()
{
  button2.read();
}

void button2Pressed()
{
  if (currentMode == MODE_SETTINGS)
  {
    decreaseVolume();
    return;
  }

  if (currentMode == MODE_FAVORITES)
  {
    playFavorite(1);
    return;
  }

  // Default behavior for other non-settings modes
  replayLastTrack();
}

void button3ISR()
{
  button3.read();
}

void button3Pressed()
{
  // Button 3 single press behavior varies by mode.
  if (currentMode == MODE_SETTINGS)
  {
    // In settings mode, keep existing play/pause mapping (togglePlayPause will no-op if inappropriate)
    togglePlayPause();
    return;
  }

  if (currentMode == MODE_FAVORITES)
  {
    playFavorite(2);
    return;
  }

  // Default: toggle play/pause
  togglePlayPause();
}

void button3longPressed()
{
  toggleSettingsMode();
}

void saveVolumeToEEPROM(uint8_t volume) {
  // Write the volume to flash storage. FlashStorage_SAMD handles wear-leveling internally.
  volumeFlash.write(volume);
}

uint8_t loadVolumeFromEEPROM() {
  // Read the saved value from flash. If uninitialized or out of range, return DEFAULT_VOLUME.
  uint8_t volume;
  volumeFlash.read(volume);
  if (volume > 30) {
    return DEFAULT_VOLUME;
  }
  return volume;
}

// Serial command handling moved out of loop() for clarity
void handleSerialCommands()
{
  // Parse any incoming serial commands from the USB serial (Serial)
  if (Serial.available())
  {
    // Read a full line (ending with '\n') and handle it.
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0)
    {
      // Handle command
      // Convert to a c-string for simple tokenization
      char buf[128];
      line.toCharArray(buf, sizeof(buf));
      // convert buffer to lowercase for case-insensitive commands
      for (char *p = buf; *p; ++p)
        *p = tolower((unsigned char)*p);
      char *token = strtok(buf, " \t");
      if (token != NULL)
      {
        // play <track>
        if (strcmp(token, "play") == 0)
        {
          char *arg = strtok(NULL, " \t");
          if (arg)
          {
            int track = atoi(arg);
            DFPlayer.play(track);
            Serial.print(F("CMD: play "));
            // Serial.println(track);
          }
          else
          {
            // Serial.println(F("ERR: play requires a track number"));
          }
        }
        // playfolder <folder> <file>
        else if (strcmp(token, "playfolder") == 0)
        {
          char *a1 = strtok(NULL, " \t");
          char *a2 = strtok(NULL, " \t");
          if (a1 && a2)
          {
            int folder = atoi(a1);
            int file = atoi(a2);
            DFPlayer.playFolder(folder, file);
            Serial.print(F("CMD: playfolder "));
            Serial.print(folder);
            Serial.print(' ');
            // Serial.println(file);
          }
          else
          {
            // Serial.println(F("ERR: playfolder requires folder and file"));
          }
        }
        // next
        else if (strcmp(token, "next") == 0)
        {
          DFPlayer.next();
          // Serial.println(F("CMD: next"));
        }
        // prev | previous
        else if (strcmp(token, "prev") == 0 || strcmp(token, "previous") == 0)
        {
          DFPlayer.previous();
          // Serial.println(F("CMD: previous"));
        }
        // pause
        else if (strcmp(token, "pause") == 0)
        {
          DFPlayer.pause();
          // Serial.println(F("CMD: pause"));
        }
        // resume | start
        else if (strcmp(token, "resume") == 0 || strcmp(token, "start") == 0)
        {
          DFPlayer.start();
          // Serial.println(F("CMD: start/resume"));
        }
        // stop
        else if (strcmp(token, "stop") == 0)
        {
          DFPlayer.stop();
          // Serial.println(F("CMD: stop"));
        }
        // volume <0-30>
        else if (strcmp(token, "volume") == 0 || strcmp(token, "vol") == 0)
        {
          char *a = strtok(NULL, " \t");
          if (a)
          {
            int v = atoi(a);
            if (v < 0)
              v = 0;
            if (v > 30)
              v = 30;
            DFPlayer.volume(v);
            saveVolumeToEEPROM(v);
            Serial.print(F("CMD: volume "));
            // Serial.println(v);
          }
          else
          {
            // Serial.println(F("ERR: volume requires a value 0-30"));
          }
        }
        // volup
        else if (strcmp(token, "volup") == 0 || strcmp(token, "volumeup") == 0)
        {
          increaseVolume();
          // Serial.println(F("CMD: volumeUp"));
        }
        // voldown
        else if (strcmp(token, "voldown") == 0 || strcmp(token, "volumedown") == 0)
        {
          decreaseVolume();
          // Serial.println(F("CMD: volumeDown"));
        }
        // eq <normal|pop|rock|jazz|classic|bass>
        else if (strcmp(token, "eq") == 0)
        {
          char *a = strtok(NULL, " \t");
          if (a)
          {
            if (strcmp(a, "normal") == 0)
              DFPlayer.EQ(DFPLAYER_EQ_NORMAL);
            else if (strcmp(a, "pop") == 0)
              DFPlayer.EQ(DFPLAYER_EQ_POP);
            else if (strcmp(a, "rock") == 0)
              DFPlayer.EQ(DFPLAYER_EQ_ROCK);
            else if (strcmp(a, "jazz") == 0)
              DFPlayer.EQ(DFPLAYER_EQ_JAZZ);
            else if (strcmp(a, "classic") == 0)
              DFPlayer.EQ(DFPLAYER_EQ_CLASSIC);
            else if (strcmp(a, "bass") == 0)
              DFPlayer.EQ(DFPLAYER_EQ_BASS);
            else
            {
              // Serial.println(F("ERR: unknown eq value"));
              a = NULL;
            }
            if (a)
            {
              Serial.print(F("CMD: eq "));
              // Serial.println(a);
            }
          }
          else
          {
            // Serial.println(F("ERR: eq requires a value"));
          }
        }
        // loopfolder <n>
        else if (strcmp(token, "loopfolder") == 0)
        {
          char *a = strtok(NULL, " \t");
          if (a)
          {
            int f = atoi(a);
            DFPlayer.loopFolder(f);
            Serial.print(F("CMD: loopFolder "));
            // Serial.println(f);
          }
          else
          {
            // Serial.println(F("ERR: loopfolder requires a folder number"));
          }
        }
        // sleep
        else if (strcmp(token, "sleep") == 0)
        {
          DFPlayer.sleep();
          // Serial.println(F("CMD: sleep"));
        }
        // reset
        else if (strcmp(token, "reset") == 0)
        {
          DFPlayer.reset();
          // Serial.println(F("CMD: reset"));
        }
        // status - read some info
        else if (strcmp(token, "status") == 0)
        {
          Serial.print(F("State: "));
          // Serial.println(DFPlayer.readState());
          Serial.print(F("Volume: "));
          // Serial.println(DFPlayer.readVolume());
          Serial.print(F("EQ: "));
          // Serial.println(DFPlayer.readEQ());
          Serial.print(F("CurrentFile: "));
          // Serial.println(DFPlayer.readCurrentFileNumber());
        }
        // help
        else if (strcmp(token, "help") == 0)
        {
          // Serial.println(F("Supported commands: play <n>, playfolder <f> <n>, next, prev, pause, resume, stop, volume <0-30>, volup, voldown, eq <normal|pop|rock|jazz|classic|bass>, loopfolder <n>, sleep, reset, status"));
        }
        else
        {
          Serial.print(F("ERR: unknown command: "));
          // Serial.println(token);
        }
      }
    }
  }
}