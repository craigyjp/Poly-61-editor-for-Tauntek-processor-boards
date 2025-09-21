/*
  TSynth patch saving and recall works like an analogue polysynth from the late 70s (Prophet 5).
  When you recall a patch, all the front panel controls will be different values from those saved in the patch. Moving them will cause a jump to the current value.

  BACK
  Cancels current mode such as save, recall, delete and rename patches

  RECALL
  Recall shows list of patches. Use encoder to move through list.
  Enter button on encoder chooses highlighted patch or press Recall again.
  Recall also recalls the current patch settings if the panel controls have been altered.
  Holding Recall for 1.5s will initialise the synth with all the current panel control settings - the synth sounds the same as the controls are set.


  SAVE
  Save will save the current settings to a new patch at the end of the list or you can use the encoder to overwrite an existing patch.
  Press Save again to save it. If you want to name/rename the patch, press the encoder enter button and use the encoder and enter button to choose an alphanumeric name.
  Holding Save for 1.5s will go into a patch deletion mode. Use encoder and enter button to choose and delete patch. Patch numbers will be changed on the SD card to be consecutive again.
*/
//Agileware CircularBuffer available in libraries manager
#include <CircularBuffer.hpp>

#define TOTALCHARS 63

const char CHARACTERS[TOTALCHARS] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', ' ', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
int charIndex = 0;
char currentCharacter = 0;
String renamedPatch = "";

struct PatchNoAndName
{
  int patchNo;
  String patchName;
};

CircularBuffer<PatchNoAndName, PATCHES_LIMIT> patches;

size_t readField(fs::File *file, char *str, size_t size, const char *delim)
{
  uint8_t ch;
  size_t n = 0;
  while ((n + 1) < size && file->read(&ch, 1) == 1)
  {
    // Delete CR
    if (ch == '\r') continue;

    str[n++] = (char)ch;
    if (strchr(delim, ch)) break;
  }
  str[n] = '\0';
  return n;
}

void recallPatchData(File& patchFile, String data[]) {
  size_t n;
  char str[64];  // Increase if any param string could be >20 chars
  int i = 0;

  while (patchFile.available() && i < NO_OF_PARAMS) {
    n = readField(&patchFile, str, sizeof(str), ",\n");

    if (n == 0) break;  // EOF or error

    if (str[n - 1] == ',' || str[n - 1] == '\n') {
      str[n - 1] = '\0';  // Trim delimiter
    }

    data[i++] = String(str);
  }

  // Fill remaining fields with empty strings to avoid uninitialized slots
  while (i < NO_OF_PARAMS) {
    data[i++] = "";
  }
}


int compare(const void *a, const void *b) {
  return ((PatchNoAndName*)a)->patchNo - ((PatchNoAndName*)b)->patchNo;
}

void sortPatches()
{
  int arraySize = patches.size();
  //Sort patches buffer to be consecutive ascending patchNo order
  struct PatchNoAndName arrayToSort[arraySize];

  for (int i = 0; i < arraySize; ++i)
  {
    arrayToSort[i] = patches[i];
  }
  qsort(arrayToSort, arraySize, sizeof(PatchNoAndName), compare);
  patches.clear();

  for (int i = 0; i < arraySize; ++i)
  {
    patches.push(arrayToSort[i]);
  }
}

void loadPatches() {
  File dir = SD.open("/");
  if (!dir || !dir.isDirectory()) {
    Serial.println("Failed to open SD root");
    return;
  }

  patches.clear();

  while (true) {
    File patchFile = dir.openNextFile();
    if (!patchFile) break;

    if (patchFile.isDirectory()) {
      Serial.println("Ignoring directory");
      continue;
    }

    String name = patchFile.name(); // e.g. "1", "2"
    if (!name.length() || !isdigit(name[0])) continue;

    int patchNo = name.toInt();
    if (patchNo == 0 && name != "0") continue;  // guard against failed toInt()

    String data[NO_OF_PARAMS];
    recallPatchData(patchFile, data);
    patches.push(PatchNoAndName{patchNo, data[0]});
    Serial.println(name + ":" + data[0]);

    patchFile.close();
  }

  sortPatches();
}

void savePatch(const char* patchNo, const String& patchData) {
  char filename[16];
  snprintf(filename, sizeof(filename), "/%s", patchNo);

  if (SD.exists(filename)) {
    SD.remove(filename);
  }

  File patchFile = SD.open(filename, FILE_WRITE);
  if (patchFile) {
    patchFile.println(patchData);
    patchFile.close();
  } else {
    Serial.print("Error writing Patch file: ");
    Serial.println(filename);
  }
}

void savePatch(int patchIndex, const String patchData[]) {
  String dataString = patchData[0];
  for (int i = 1; i < NO_OF_PARAMS; i++) {
    dataString += "," + patchData[i];
  }

  char patchNo[8];
  snprintf(patchNo, sizeof(patchNo), "%03d", patchIndex);
  savePatch(patchNo, dataString);
}

void deletePatch(int patchIndex) {
  char filename[16];
  snprintf(filename, sizeof(filename), "/%03d", patchIndex);
  if (SD.exists(filename)) SD.remove(filename);
}

void renumberPatchesOnSD() {
  for (int i = 0; i < patches.size(); i++) {
    String data[NO_OF_PARAMS];
    char oldFilename[16];
    snprintf(oldFilename, sizeof(oldFilename), "/%03d", patches[i].patchNo);

    File file = SD.open(oldFilename);
    if (file) {
      recallPatchData(file, data);
      file.close();
      savePatch(i + 1, data);  // uses index+1 as new patch number
    }
  }

  deletePatch(patches.size() + 1);  // delete the trailing duplicate
}


void setPatchesOrdering(int no) {
  if (patches.size() < 2)return;
  while (patches.first().patchNo != no) {
    patches.push(patches.shift());
  }
}

void resetPatchesOrdering() {
  setPatchesOrdering(1);
}
