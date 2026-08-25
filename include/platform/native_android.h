#ifndef NATIVE_ANDROID_H
#define NATIVE_ANDROID_H

#ifdef __ANDROID__

void Platform_Android_PickFile(void);
char *Platform_Android_GetStoredPath(void);
int Platform_Android_IsPickerActive(void);
void Platform_Android_ApplyTouchButtons(int slot, u16 buttons);

#endif // __ANDROID__

#endif
