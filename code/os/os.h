#if !defined (OS_HELPER_H)
#define OS_HELPER_H

typedef u32 OS_InputFlag;
enum
{
  OS_InputFlag_Pressed,
  OS_InputFlag_Released,
  OS_InputFlag_Held,
};

typedef u32 OS_KeyType;
enum
{
  OS_KeyType_Esc,
  OS_KeyType_Space,
  OS_KeyType_LShift,
  OS_KeyType_A,
  OS_KeyType_D,
  OS_KeyType_S,
  OS_KeyType_X,
  OS_KeyType_W,
  OS_KeyType_Count,
};

typedef u32 OS_ButtonType;
enum
{
  OS_ButtonType_Left,
  OS_ButtonType_Right,
  OS_ButtonType_Count,
};

#endif
