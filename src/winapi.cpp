#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <unordered_map>
#include <list>
#include <nan.h>
#include <iostream>
#include "string_cast.h"

using namespace Nan;
using namespace v8;


static std::wstring strerror(DWORD errorno) {
  wchar_t *errmsg = nullptr;

  LCID lcid;
  GetLocaleInfoEx(L"en-US", LOCALE_RETURN_NUMBER | LOCALE_ILANGUAGE, reinterpret_cast<LPWSTR>(&lcid), sizeof(lcid));

  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
    FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, errorno,
    lcid, (LPWSTR)&errmsg, 0, nullptr);

  if (errmsg) {
    for (int i = (wcslen(errmsg) - 1);
         (i >= 0) && ((errmsg[i] == '\n') || (errmsg[i] == '\r'));
         --i) {
      errmsg[i] = '\0';
    }

    return errmsg;
  }
  else {
    return L"Unknown error";
  }
}

inline v8::Local<v8::Value> WinApiException(
  DWORD lastError
  , const char *func = nullptr
  , const char* path = nullptr) {

  std::wstring errStr = strerror(lastError);
  std::string err = toMB(errStr.c_str(), CodePage::UTF8, errStr.size());
  return node::WinapiErrnoException(v8::Isolate::GetCurrent(), lastError, func, err.c_str(), path);
}

Local<String> operator "" _n(const char *input, size_t) {
  return Nan::New(input).ToLocalChecked();
}

std::wstring toWC(const Local<Value> &input) {
  if (input->IsNullOrUndefined()) {
    return std::wstring();
  }
  String::Utf8Value temp(input);
  return toWC(*temp, CodePage::UTF8, temp.length());
}


DWORD mapAttributes(Local<Array> input) {
  static const std::unordered_map<std::string, DWORD> attributeMap{
    { "archive", FILE_ATTRIBUTE_ARCHIVE },
    { "hidden", FILE_ATTRIBUTE_HIDDEN },
    { "normal", FILE_ATTRIBUTE_NORMAL },
    { "not_content_indexed", FILE_ATTRIBUTE_NOT_CONTENT_INDEXED },
    { "readonly", FILE_ATTRIBUTE_READONLY },
    { "temporary", FILE_ATTRIBUTE_TEMPORARY },
  };

  DWORD res = 0;
  for (uint32_t i = 0; i < input->Length(); ++i) {
    v8::String::Utf8Value attr(input->Get(i)->ToString());

    auto attribute = attributeMap.find(*attr);
    if (attribute != attributeMap.end()) {
      res |= attribute->second;
    }
  }

  return res;
}

NAN_METHOD(SetFileAttributes) {
  Isolate* isolate = Isolate::GetCurrent();
  try {
    if (info.Length() != 2) {
      Nan::ThrowError("Expected two parameters (path, attributes)");
      return;
    }

    String::Utf8Value pathV8(info[0]->ToString());
    std::wstring path = toWC(*pathV8, CodePage::UTF8, pathV8.length());
    Local<Array> attributes = Local<Array>::Cast(info[1]);

    if (!::SetFileAttributesW(path.c_str(), mapAttributes(attributes))) {
      isolate->ThrowException(WinApiException(::GetLastError(), "SetFileAttributes", *pathV8));
      return;
    }
  }
  catch (const std::exception &e) {
    Nan::ThrowError(e.what());
  }
}

NAN_METHOD(GetDiskFreeSpaceEx) {
  Isolate* isolate = Isolate::GetCurrent();

  try {
    if (info.Length() != 1) {
      Nan::ThrowError("Expected one parameter (path)");
      return;
    }

    String::Utf8Value pathV8(info[0]->ToString());
    std::wstring path = toWC(*pathV8, CodePage::UTF8, pathV8.length());

    ULARGE_INTEGER freeBytesAvailableToCaller;
    ULARGE_INTEGER totalNumberOfBytes;
    ULARGE_INTEGER totalNumberOfFreeBytes;

    if (!::GetDiskFreeSpaceExW(path.c_str(),
      &freeBytesAvailableToCaller,
      &totalNumberOfBytes,
      &totalNumberOfFreeBytes)) {
      isolate->ThrowException(WinApiException(::GetLastError(), "GetDiskFreeSpaceEx", *pathV8));
      return;
    }

    Local<Object> result = New<Object>();
    result->Set("total"_n, New<Number>(static_cast<double>(totalNumberOfBytes.QuadPart)));
    result->Set("free"_n, New<Number>(static_cast<double>(totalNumberOfFreeBytes.QuadPart)));
    result->Set("freeToCaller"_n, New<Number>(static_cast<double>(freeBytesAvailableToCaller.QuadPart)));

    info.GetReturnValue().Set(result);
  }
  catch (const std::exception &e) {
    Nan::ThrowError(e.what());
  }
}

NAN_METHOD(GetVolumePathName) {
  Isolate* isolate = Isolate::GetCurrent();

  try {
    if (info.Length() != 1) {
      Nan::ThrowError("Expected one parameter (path)");
      return;
    }

    std::wstring path = toWC(info[0]);

    wchar_t buffer[MAX_PATH];
    if (!::GetVolumePathNameW(path.c_str(), buffer, MAX_PATH)) {
      isolate->ThrowException(WinApiException(::GetLastError(), "GetDiskFreeSpaceEx", toMB(path.c_str(), CodePage::UTF8, path.length()).c_str()));
      return;
    }

    info.GetReturnValue().Set(New<String>(toMB(buffer, CodePage::UTF8, (std::numeric_limits<size_t>::max)())).ToLocalChecked());
  }
  catch (const std::exception &e) {
    Nan::ThrowError(e.what());
  }
}


uint32_t translateExecuteMask(const std::string &name) {
  static std::unordered_map<std::string, uint32_t> map{
    { "noasync", SEE_MASK_NOASYNC },
    { "flag_no_ui", SEE_MASK_FLAG_NO_UI },
    { "unicode", SEE_MASK_UNICODE },
    { "no_console", SEE_MASK_NO_CONSOLE },
    { "waitforinputidle", SEE_MASK_WAITFORINPUTIDLE }
  };

  auto iter = map.find(name);
  if (iter != map.end()) {
    return iter->second;
  }

  return 0;
}

NAN_METHOD(ShellExecuteEx) {
  static const std::unordered_map<std::string, DWORD> showFlagMap{
    {"hide", SW_HIDE},
    {"maximize", SW_MAXIMIZE},
    {"minimize", SW_MINIMIZE},
    {"restore", SW_RESTORE},
    {"show", SW_SHOW},
    {"showdefault", SW_SHOWDEFAULT},
    {"showminimized", SW_SHOWMINIMIZED},
    {"showminnoactive", SW_SHOWMINNOACTIVE},
    {"showna", SW_SHOWNA},
    {"shownoactivate", SW_SHOWNOACTIVATE},
    {"shownormal", SW_SHOWNORMAL},
  };

  Isolate *isolate = Isolate::GetCurrent();

  try {
    if (info.Length() != 1) {
      Nan::ThrowError("Expected one parameter (options)");
      return;
    }

    Local<Object> args(info[0]->ToObject());

    if (!args->Has("file"_n) || !args->Has("show"_n)) {
      Nan::ThrowError("Parameter missing (required: file, show)");
      return;
    }

    // important: has to be a container that doesn't invalidate iterators on insertion (like vector would)
    std::list<std::wstring> buffers;

    auto assignParameter = [&args, &buffers](LPCWSTR &target, const Local<Value> &key) {
      if (args->Has(key)) {
        String::Utf8Value value(args->Get(key)->ToString());
        buffers.push_back(toWC(*value, CodePage::UTF8, value.length()));
        target = buffers.rbegin()->c_str();
      }
      else {
        target = nullptr;
      }
    };

    SHELLEXECUTEINFOW execInfo;
    ZeroMemory(&execInfo, sizeof(SHELLEXECUTEINFOW));
    execInfo.cbSize = sizeof(SHELLEXECUTEINFO);

    execInfo.fMask = 0;

    if ((args->Has("mask"_n) && args->Get("mask"_n)->IsArray())) {
      Local<Array> mask = Local<Array>::Cast(args->Get("mask"_n));
      for (uint32_t i = 0; i < mask->Length(); ++i) {
        Local<Value> val = mask->Get(i);
        if (val->IsString()) {
          execInfo.fMask |= translateExecuteMask(*Utf8String(val->ToString()));
        }
        else {
          execInfo.fMask |= val->Uint32Value();
        }
      }
    }

    execInfo.hwnd = nullptr;
    execInfo.hInstApp = nullptr;

    assignParameter(execInfo.lpVerb, "verb"_n);
    assignParameter(execInfo.lpFile, "file"_n);
    assignParameter(execInfo.lpDirectory, "directory"_n);
    assignParameter(execInfo.lpParameters, "parameters"_n);

    v8::String::Utf8Value show(args->Get("show"_n)->ToString());
    auto iter = showFlagMap.find(*show);
    if (iter == showFlagMap.end()) {
      Nan::ThrowRangeError("Invalid show flag");
      return;
    }
    execInfo.nShow = iter->second;


    if (!::ShellExecuteExW(&execInfo)) {
      std::string fileName = toMB(execInfo.lpFile, CodePage::UTF8, wcslen(execInfo.lpFile));
      isolate->ThrowException(WinApiException(::GetLastError(), "ShellExecuteEx", fileName.c_str()));
      return;
    }
  }
  catch (const std::exception &e) {
    Nan::ThrowError(e.what());
  }
}

NAN_METHOD(GetPrivateProfileSection) {
  Isolate* isolate = Isolate::GetCurrent();

  try {
    if (info.Length() != 2) {
      Nan::ThrowError("Expected two parameters (section, fileName)");
      return;
    }

    String::Utf8Value appNameV8(info[0]->ToString());
    String::Utf8Value fileNameV8(info[1]->ToString());

    std::wstring appName = toWC(*appNameV8, CodePage::UTF8, appNameV8.length());
    std::wstring fileName = toWC(*fileNameV8, CodePage::UTF8, fileNameV8.length());

    DWORD size = 32 * 1024;
    std::unique_ptr<wchar_t[]> buffer(new wchar_t[size]);

    DWORD charCount = ::GetPrivateProfileSectionW(appName.c_str(), buffer.get(), size, fileName.c_str());

    Local<Object> result = New<Object>();
    wchar_t *start = buffer.get();
    wchar_t *ptr = start;
    // double check. the list is supposed to end on a double zero termination but to ensure we don't overrun
    // the buffer, also verify we don't exceed the character count
    Local<String> lastKey;
    Local<String> lastValue;
    while ((*ptr != '\0') && ((ptr - start) < charCount)) {
      wchar_t *eqPos = wcschr(ptr, L'=');
      size_t valLength;
      if (eqPos != nullptr) {
        lastKey = New<String>(toMB(ptr, CodePage::UTF8, eqPos - ptr)).ToLocalChecked();
        valLength = wcslen(eqPos);
        lastValue = New<String>(toMB(eqPos + 1, CodePage::UTF8, valLength - 1)).ToLocalChecked();
        ptr = eqPos + valLength + 1;
        result->Set(lastKey, lastValue);
      }
      else {
        // ignore all lines that contain no equal sign
        ptr += wcslen(ptr) + 1;
      }
    }

    info.GetReturnValue().Set(result);
  }
  catch (const std::exception &e) {
    Nan::ThrowError(e.what());
  }
}

Local<Array> convertMultiSZ(wchar_t *input, DWORD maxLength) {
  Local<Array> result = New<Array>();
  wchar_t *start = input;
  wchar_t *ptr = start;
  int idx = 0;
  // double check. the list is supposed to end on a double zero termination but to ensure we don't overrun
  // the buffer, also verify we don't exceed the character count
  while ((*ptr != '\0') && ((ptr - start) < maxLength)) {
    size_t len = wcslen(ptr);
    result->Set(idx++, New<String>(toMB(ptr, CodePage::UTF8, len)).ToLocalChecked());
    ptr += len + 1;
  }

  return result;
}

NAN_METHOD(GetPrivateProfileSectionNames) {
  Isolate* isolate = Isolate::GetCurrent();

  try {
    if (info.Length() != 1) {
      Nan::ThrowError("Expected one parameter (fileName)");
      return;
    }

    String::Utf8Value fileName(info[0]->ToString());

    DWORD size = 32 * 1024;
    std::unique_ptr<wchar_t[]> buffer(new wchar_t[size]);

    DWORD charCount = ::GetPrivateProfileSectionNamesW(buffer.get(), size,
      toWC(*fileName, CodePage::UTF8, fileName.length()).c_str());

    info.GetReturnValue().Set(convertMultiSZ(buffer.get(), charCount));
  }
  catch (const std::exception &e) {
    Nan::ThrowError(e.what());
  }
}

NAN_METHOD(GetPrivateProfileString) {
  Isolate* isolate = Isolate::GetCurrent();

  try {
    if (info.Length() != 4) {
      Nan::ThrowError("Expected four parameters (section, key, default, fileName)");
      return;
    }

    std::wstring appName = toWC(info[0]);
    std::wstring keyName = toWC(info[1]);
    std::wstring defaultValue = toWC(info[2]);
    std::wstring fileName = toWC(info[3]);

    DWORD size = 32 * 1024;
    std::unique_ptr<wchar_t[]> buffer(new wchar_t[size]);

    bool repeat = true;

    DWORD charCount = 0;

    while (repeat) {
      charCount = ::GetPrivateProfileStringW(
        appName.c_str(), keyName.c_str(), defaultValue.c_str(),
        buffer.get(), size, fileName.c_str());
      if (charCount == 0) {
        DWORD error = ::GetLastError();
        if (error != ERROR_SUCCESS) {
          isolate->ThrowException(WinApiException(::GetLastError(), "GetPrivateProfileString", toMB(fileName.c_str(), CodePage::UTF8, fileName.length()).c_str()));
          return;
        }
      }
      if (charCount < size - 1) {
        repeat = false;
      }
      else {
        size *= 2;
        buffer.reset(new wchar_t[size]);
      }
    }
    info.GetReturnValue().Set(convertMultiSZ(buffer.get(), charCount));
  }
  catch (const std::exception &e) {
    Nan::ThrowError(e.what());
  }
}

NAN_METHOD(WritePrivateProfileString) {
  Isolate* isolate = Isolate::GetCurrent();

  try {
    if (info.Length() != 4) {
      Nan::ThrowError("Expected four parameters (section, key, value, fileName)");
      return;
    }

    String::Utf8Value appNameV8(info[0]->ToString());
    String::Utf8Value keyNameV8(info[1]->ToString());
    String::Utf8Value valueV8(info[2]->ToString());
    String::Utf8Value fileNameV8(info[3]->ToString());

    std::wstring appName = toWC(*appNameV8, CodePage::UTF8, appNameV8.length());
    std::wstring keyName = toWC(*keyNameV8, CodePage::UTF8, keyNameV8.length());
    std::wstring value = info[2]->IsNullOrUndefined() ? L"" : toWC(*valueV8, CodePage::UTF8, valueV8.length());
    std::wstring fileName = toWC(*fileNameV8, CodePage::UTF8, fileNameV8.length());

    BOOL res = ::WritePrivateProfileStringW(appName.c_str(), keyName.c_str(),
      info[2]->IsNullOrUndefined() ? nullptr : value.c_str(), fileName.c_str());

    if (!res) {
      isolate->ThrowException(WinApiException(::GetLastError(), "WritePrivateProfileString", *fileNameV8));
      return;
    }
  }
  catch (const std::exception &e) {
    Nan::ThrowError(e.what());
  }
}

static const std::unordered_map<std::string, HKEY> hkeyMap{
    { "HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT },
    { "HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG },
    { "HKEY_CURRENT_USER", HKEY_CURRENT_USER },
    { "HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE },
    { "HKEY_USERS", HKEY_USERS },
};

NAN_METHOD(WithRegOpen) {
  Isolate* isolate = Isolate::GetCurrent();

  try {
    if (info.Length() != 3) {
      Nan::ThrowError("Expected three parameters (hive, path, callback)");
      return;
    }

    String::Utf8Value hiveV8(info[0]->ToString());
    String::Utf8Value pathV8(info[1]->ToString());
    v8::Local<v8::Function> cb = info[2].As<v8::Function>();

    auto iter = hkeyMap.find(*hiveV8);
    std::wstring path = toWC(*pathV8, CodePage::UTF8, pathV8.length());

    HKEY key;
    LSTATUS res = ::RegOpenKeyExW(iter->second, path.c_str(), 0, KEY_READ, &key);
    if (res != ERROR_SUCCESS) {
      isolate->ThrowException(WinApiException(res, "WithRegOpen", *pathV8));
      return;
    }

    auto buf = CopyBuffer(reinterpret_cast<char*>(&key), sizeof(HKEY)).ToLocalChecked();
    Local<Value> argv[1] = { buf };
    AsyncResource async("callback");
    v8::Local<v8::Object> target = New<v8::Object>();
    async.runInAsyncScope(target, cb, 1, argv);

    ::RegCloseKey(key);
  }
  catch (const std::exception &e) {
    Nan::ThrowError(e.what());
  }
}

Local<String> regTypeToString(DWORD type) {
  switch (type) {
    case REG_BINARY: return "REG_BINARY"_n;
    case REG_DWORD: return "REG_DWORD"_n;
    case REG_DWORD_BIG_ENDIAN: return "REG_DWORD_BIG_ENDIAN"_n;
    case REG_EXPAND_SZ: return "REG_EXPAND_SZ"_n;
    case REG_LINK: return "REG_LINK"_n;
    case REG_MULTI_SZ: return "REG_MULTI_SZ"_n;
    case REG_NONE: return "REG_NONE"_n;
    case REG_QWORD: return "REG_QWORD"_n;
    case REG_SZ: return "REG_SZ"_n;
    default: throw std::runtime_error("invalid registry type");
  }
}

uint64_t toTimestamp(FILETIME ft)
{
  LARGE_INTEGER date;
  date.HighPart = ft.dwHighDateTime;
  date.LowPart = ft.dwLowDateTime;

  date.QuadPart -= (11644473600000 * 10000);

  return date.QuadPart / 10000;
}

NAN_METHOD(RegGetValue) {
  Isolate* isolate = Isolate::GetCurrent();

  try {
    if (info.Length() != 3) {
      Nan::ThrowError("Expected three parameters (key, subkey, value)");
      return;
    }

    HKEY key;
    if (info[0]->IsString()) {
      String::Utf8Value hkeyStr(info[0]->ToString());
      auto iter = hkeyMap.find(*hkeyStr);
      key = iter->second;
    }
    else {
      memcpy(&key, node::Buffer::Data(info[0]), sizeof(HKEY));
    }

    String::Utf8Value pathV8(info[1]->ToString());
    String::Utf8Value valueV8(info[2]->ToString());

    std::wstring path = toWC(*pathV8, CodePage::UTF8, pathV8.length());
    std::wstring value = toWC(*valueV8, CodePage::UTF8, valueV8.length());

    DWORD type;
    DWORD dataSize = 0;

    LSTATUS res = ::RegGetValueW(key, path.c_str(), value.c_str(), RRF_RT_ANY, &type, nullptr, &dataSize);
    if (res != ERROR_SUCCESS) {
      isolate->ThrowException(WinApiException(res, "RegGetValue", *pathV8));
      return;
    }

    std::shared_ptr<uint8_t[]> buffer(new uint8_t[dataSize]);

    res = ::RegGetValueW(key, path.c_str(), value.c_str(), RRF_RT_ANY, &type, buffer.get(), &dataSize);

    if (res != ERROR_SUCCESS) {
      isolate->ThrowException(WinApiException(res, "RegGetValue", *pathV8));
      return;
    }

    Local<Object> result = New<Object>();
    result->Set("type"_n, regTypeToString(type));

    switch (type) {
      case REG_BINARY: {
        result->Set("value"_n, CopyBuffer(reinterpret_cast<char*>(buffer.get()), dataSize).ToLocalChecked());
      } break;
      case REG_DWORD: {
        DWORD val = *reinterpret_cast<DWORD*>(buffer.get());
        result->Set("value"_n, New<Number>(val));
      } break;
      case REG_DWORD_BIG_ENDIAN: {
        union {
          DWORD val;
          char temp[4];
        };
        for (int i = 0; i < 4; ++i) {
          temp[i] = buffer[3 - i];
        }
        result->Set("value"_n, New<Number>(val));
      } break;
      case REG_MULTI_SZ: {
        result->Set("value"_n, convertMultiSZ(reinterpret_cast<wchar_t*>(buffer.get()), dataSize));
      } break;
      case REG_NONE: { } break;
      case REG_QWORD: {
        result->Set("value"_n, New<Number>(static_cast<double>(*reinterpret_cast<uint64_t*>(buffer.get()))));
      } break;
      case REG_SZ:
      case REG_EXPAND_SZ:
      case REG_LINK: {
        const wchar_t *buf = reinterpret_cast<wchar_t*>(buffer.get());
        result->Set("value"_n, New<String>(toMB(buf, CodePage::UTF8, (dataSize / sizeof(wchar_t)) - 1)).ToLocalChecked());
      } break;
    }

    info.GetReturnValue().Set(result);
  }
  catch (const std::exception &e) {
    Nan::ThrowError(e.what());
  }
}

NAN_METHOD(RegEnumKeys) {
  Isolate* isolate = Isolate::GetCurrent();

  try {
    if (info.Length() != 1) {
      Nan::ThrowError("Expected one parameters (key)");
      return;
    }

    HKEY key;
    memcpy(&key, node::Buffer::Data(info[0]), sizeof(HKEY));

    DWORD numSubkeys;
    DWORD maxSubkeyLen;
    DWORD maxClassLen;
    LSTATUS res = RegQueryInfoKey(key, nullptr, nullptr, nullptr, &numSubkeys, &maxSubkeyLen, &maxClassLen, nullptr, nullptr, nullptr, nullptr, nullptr);
    if (res != ERROR_SUCCESS) {
      isolate->ThrowException(WinApiException(res, "RegEnumKeys"));
      return;
    }

    Local<Array> result = New<Array>();
    std::shared_ptr<wchar_t[]> keyBuffer(new wchar_t[maxSubkeyLen + 1]);
    std::shared_ptr<wchar_t[]> classBuffer(new wchar_t[maxClassLen + 1]);
    for (DWORD i = 0; i < numSubkeys; ++i) {
      DWORD keyLen = maxSubkeyLen + 1;
      DWORD classLen = maxClassLen + 1;
      FILETIME lastWritten;
      res = ::RegEnumKeyExW(key, i, keyBuffer.get(), &keyLen, nullptr, classBuffer.get(), &classLen, &lastWritten);
      if (res != ERROR_SUCCESS) {
        isolate->ThrowException(WinApiException(res, "RegEnumKeys"));
        return;
      }

      Local<Object> item = New<Object>();
      item->Set("class"_n, New<String>(toMB(classBuffer.get(), CodePage::UTF8, classLen)).ToLocalChecked());
      item->Set("key"_n, New<String>(toMB(keyBuffer.get(), CodePage::UTF8, keyLen)).ToLocalChecked());
      item->Set("lastWritten"_n, New<Number>(static_cast<double>(toTimestamp(lastWritten))));
      result->Set(i, item);
    }

    info.GetReturnValue().Set(result);
  }
  catch (const std::exception &e) {
    Nan::ThrowError(e.what());
  }
}

NAN_METHOD(RegEnumValues) {
  Isolate* isolate = Isolate::GetCurrent();

  try {
    if (info.Length() != 1) {
      Nan::ThrowError("Expected one parameters (key)");
      return;
    }

    HKEY key;
    memcpy(&key, node::Buffer::Data(info[0]), sizeof(HKEY));

    DWORD numValues;
    DWORD maxKeyLen;
    LSTATUS res = RegQueryInfoKey(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &numValues, &maxKeyLen, nullptr, nullptr, nullptr);
    if (res != ERROR_SUCCESS) {
      isolate->ThrowException(WinApiException(res, "RegEnumValues"));
      return;
    }

    Local<Array> result = New<Array>();
    std::shared_ptr<wchar_t[]> keyBuffer(new wchar_t[maxKeyLen + 1]);
    for (DWORD i = 0; i < numValues; ++i) {
      DWORD keyLen = maxKeyLen + 1;
      DWORD type;
      res = ::RegEnumValueW(key, i, keyBuffer.get(), &keyLen, nullptr, &type, nullptr, nullptr);
      if (res != ERROR_SUCCESS) {
        isolate->ThrowException(WinApiException(res, "RegEnumValues"));
        return;
      }

      Local<Object> item = New<Object>();
      item->Set("type"_n, regTypeToString(type));
      item->Set("key"_n, New<String>(toMB(keyBuffer.get(), CodePage::UTF8, keyLen)).ToLocalChecked());
      result->Set(i, item);
    }

    info.GetReturnValue().Set(result);
  }
  catch (const std::exception &e) {
    Nan::ThrowError(e.what());
  }
}

NAN_MODULE_INIT(Init) {
  Nan::Set(target, "SetFileAttributes"_n,
    GetFunction(New<FunctionTemplate>(SetFileAttributes)).ToLocalChecked());
  Nan::Set(target, "GetDiskFreeSpaceEx"_n,
    GetFunction(New<FunctionTemplate>(GetDiskFreeSpaceEx)).ToLocalChecked());
  Nan::Set(target, "GetVolumePathName"_n,
    GetFunction(New<FunctionTemplate>(GetVolumePathName)).ToLocalChecked());
  Nan::Set(target, "ShellExecuteEx"_n,
    GetFunction(New<FunctionTemplate>(ShellExecuteEx)).ToLocalChecked());
  Nan::Set(target, "GetPrivateProfileSection"_n,
    GetFunction(New<FunctionTemplate>(GetPrivateProfileSection)).ToLocalChecked());
  Nan::Set(target, "GetPrivateProfileSectionNames"_n,
    GetFunction(New<FunctionTemplate>(GetPrivateProfileSectionNames)).ToLocalChecked());
  Nan::Set(target, "GetPrivateProfileString"_n,
    GetFunction(New<FunctionTemplate>(GetPrivateProfileString)).ToLocalChecked());

  Nan::Set(target, "WritePrivateProfileString"_n,
    GetFunction(New<FunctionTemplate>(WritePrivateProfileString)).ToLocalChecked());

  Nan::Set(target, "WithRegOpen"_n,
    GetFunction(New<FunctionTemplate>(WithRegOpen)).ToLocalChecked());
  Nan::Set(target, "RegGetValue"_n,
    GetFunction(New<FunctionTemplate>(RegGetValue)).ToLocalChecked());
  Nan::Set(target, "RegEnumKeys"_n,
    GetFunction(New<FunctionTemplate>(RegEnumKeys)).ToLocalChecked());
  Nan::Set(target, "RegEnumValues"_n,
    GetFunction(New<FunctionTemplate>(RegEnumValues)).ToLocalChecked());
}

NODE_MODULE(winapi, Init)
