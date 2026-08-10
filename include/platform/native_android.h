#ifndef NATIVE_ANDROID_H
#define NATIVE_ANDROID_H

#ifdef __ANDROID__

void Platform_Android_PickFile(void);
char *Platform_Android_GetStoredPath(void);

#endif // __ANDROID__

#endif
