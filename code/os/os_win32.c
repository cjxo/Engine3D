static OS_InputFlag g_input_key[OS_KeyType_Count] = { 0 };

static OS_KeyType
w32_map_wparam_to_keytype(WPARAM wParam)
{
  OS_KeyType key = OS_KeyType_Count;
  switch (wParam)
  {
    case VK_ESCAPE:
    {
      key = OS_KeyType_Esc;
    } break;

    case VK_SPACE:
    {
      key = OS_KeyType_Space;
    } break;

    case 'A':
    {
      key = OS_KeyType_A;
    } break;

    case 'D':
    {
      key = OS_KeyType_D;
    } break;

    case 'S':
    {
      key = OS_KeyType_S;
    } break;

    case 'X':
    {
      key = OS_KeyType_X;
    } break;

    case 'W':
    {
      key = OS_KeyType_W;
    } break;
  }

  return(key);
}

static LRESULT
w32_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
  LRESULT result = 0;

  switch (message)
  {
    case WM_CLOSE:
    {
      DestroyWindow(window);
    } break;
    
    case WM_DESTROY:
    {
      PostQuitMessage(0);
    } break;
    
    default:
    {
      result = DefWindowProc(window, message, wparam, lparam);
    } break;
  }
  
  return(result);
}

static void
os_input_fill_events(void)
{
  for (u32 key = 0; key < OS_KeyType_Count; ++key)
  {
    g_input_key[key] &= ~(OS_InputFlag_Pressed | OS_InputFlag_Released);
  }

  MSG msg;
  while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE) != 0)
  {
    switch (msg.message)
    {
      case WM_QUIT:
      {
        ExitProcess(0);
      } break;

      case WM_KEYDOWN:
      {
        OS_KeyType key = w32_map_wparam_to_keytype(msg.wParam);
        if (key != OS_KeyType_Count)
        {
          g_input_key[key] |= OS_InputFlag_Pressed | OS_InputFlag_Held;
        }
      } break;

      case WM_KEYUP:
      {
        OS_KeyType key = w32_map_wparam_to_keytype(msg.wParam);
        if (key != OS_KeyType_Count)
        {
          g_input_key[key] |= OS_InputFlag_Released;
          g_input_key[key] &= ~OS_InputFlag_Held;
        }
      } break;

      default:
      {
        TranslateMessage(&msg); 
        DispatchMessage(&msg);
      } break;
    }
  }
}