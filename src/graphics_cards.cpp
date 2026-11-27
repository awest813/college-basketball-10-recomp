// ncaabb10 - the graphics cards the renderer can use, and which one to prefer.
//
// The SDK takes the first hardware adapter the system lists (Direct3D 12) or the first device that
// has the features it wants (Vulkan). On a laptop with a dedicated card that is usually the one
// built into the processor, which runs this game at a fraction of the frame rate. The front end
// lists the cards itself, before the renderer starts, so it can hand the renderer the fast one and
// Options can offer the others.

#include "graphics_cards.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <rex/logging/log_category.h>

#if defined(_WIN32)
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#else
#include <dlfcn.h>
#endif

namespace ncaabb10 {
namespace {

#if defined(_WIN32)

std::string Narrow(const wchar_t* text) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) {
    return {};
  }
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  out.resize(static_cast<size_t>(size - 1));
  return out;
}

// The libraries stay loaded: the renderer loads them next, and unloading the graphics driver in
// between gains nothing.
IDXGIFactory1* CreateFactory() {
  static const auto create = [] {
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    using Fn = HRESULT(WINAPI*)(REFIID, void**);
    return dxgi ? reinterpret_cast<Fn>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
  }();
  IDXGIFactory1* factory = nullptr;
  if (!create || FAILED(create(IID_PPV_ARGS(&factory)))) {
    return nullptr;
  }
  return factory;
}

// Whether the adapter at this EnumAdapters1 index can make a Direct3D 12 device at feature level
// 11_0, as the renderer requires. That loads the card's driver (hundreds of milliseconds), so it is
// asked only of the card about to be used, whose driver the renderer loads next anyway.
bool CanRenderOn(int index) {
  static const auto create_device = [] {
    HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
    return d3d12 ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(
                       GetProcAddress(d3d12, "D3D12CreateDevice"))
                 : nullptr;
  }();
  IDXGIFactory1* factory = CreateFactory();
  if (!factory || !create_device) {
    if (factory) {
      factory->Release();
    }
    return false;
  }
  bool can = false;
  IDXGIAdapter1* adapter = nullptr;
  if (factory->EnumAdapters1(static_cast<UINT>(index), &adapter) == S_OK) {
    can = SUCCEEDED(
        create_device(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));
    adapter->Release();
  }
  factory->Release();
  return can;
}

// The renderer's list: every hardware adapter from EnumAdapters1, by that index.
GraphicsCards List() {
  GraphicsCards out;
  out.api = "Direct3D 12";
  out.setting = "gpu.d3d12.adapter";
  IDXGIFactory1* factory = CreateFactory();
  if (!factory) {
    return out;
  }
  std::vector<LUID> luids;
  IDXGIAdapter1* adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
    DXGI_ADAPTER_DESC1 desc;
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
      out.cards.push_back({Narrow(desc.Description), static_cast<int>(i)});
      luids.push_back(desc.AdapterLuid);
    }
    adapter->Release();
  }
  // Windows ranks the adapters for performance, dedicated cards first (Windows 10 1803 and
  // later; before, the listing order stands).
  std::vector<int> order;
  IDXGIFactory6* factory6 = nullptr;
  if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
    for (UINT i = 0; factory6->EnumAdapterByGpuPreference(
                         i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == S_OK;
         ++i) {
      DXGI_ADAPTER_DESC1 desc;
      if (SUCCEEDED(adapter->GetDesc1(&desc))) {
        for (size_t c = 0; c < luids.size(); ++c) {
          if (luids[c].LowPart == desc.AdapterLuid.LowPart &&
              luids[c].HighPart == desc.AdapterLuid.HighPart) {
            order.push_back(static_cast<int>(c));
          }
        }
      }
      adapter->Release();
    }
    factory6->Release();
  }
  factory->Release();
  for (size_t c = 0; c < out.cards.size(); ++c) {
    if (std::find(order.begin(), order.end(), static_cast<int>(c)) == order.end()) {
      order.push_back(static_cast<int>(c));
    }
  }
  // The fastest card that can run the renderer. A card too old for Direct3D 12 stays listed, so
  // Options can show it, and CanRender turns it down if the player picks it.
  for (const int c : order) {
    if (CanRenderOn(out.cards[static_cast<size_t>(c)].index)) {
      out.preferred = c;
      break;
    }
  }
  return out;
}

#else

// The few Vulkan 1.0 declarations the listing needs, as the specification defines them: the SDK
// package does not ship the Vulkan headers, and the loader is opened at run time like the
// renderer does.
namespace vk {
using Instance = struct InstanceHandle*;
using PhysicalDevice = struct PhysicalDeviceHandle*;
struct ApplicationInfo {
  int32_t type;  // VK_STRUCTURE_TYPE_APPLICATION_INFO, 0
  const void* next;
  const char* application_name;
  uint32_t application_version;
  const char* engine_name;
  uint32_t engine_version;
  uint32_t api_version;
};
struct InstanceCreateInfo {
  int32_t type;  // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 1
  const void* next;
  uint32_t flags;
  const ApplicationInfo* application_info;
  uint32_t layer_count;
  const char* const* layer_names;
  uint32_t extension_count;
  const char* const* extension_names;
};
// The start of VkPhysicalDeviceProperties; the limits after it are not needed.
struct PropertiesStart {
  uint32_t api_version;
  uint32_t driver_version;
  uint32_t vendor_id;
  uint32_t device_id;
  int32_t device_type;
  char device_name[256];
};
constexpr int32_t kDiscreteGpu = 2, kCpu = 4;
using VoidFunction = void (*)();
using GetInstanceProcAddr = VoidFunction (*)(Instance, const char*);
using CreateInstance = int32_t (*)(const InstanceCreateInfo*, const void*, Instance*);
using DestroyInstance = void (*)(Instance, const void*);
using EnumeratePhysicalDevices = int32_t (*)(Instance, uint32_t*, PhysicalDevice*);
using GetPhysicalDeviceProperties = void (*)(PhysicalDevice, void*);
}  // namespace vk

// Every device vkEnumeratePhysicalDevices lists, by that index, but CPU renderers (llvmpipe). The
// loader stays loaded: the renderer opens it next.
GraphicsCards List() {
  GraphicsCards out;
  out.api = "Vulkan";
  out.setting = "gpu.vulkan.device";
  void* loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!loader) {
    return out;
  }
  const auto get_proc =
      reinterpret_cast<vk::GetInstanceProcAddr>(dlsym(loader, "vkGetInstanceProcAddr"));
  if (!get_proc) {
    return out;
  }
  const auto create =
      reinterpret_cast<vk::CreateInstance>(get_proc(nullptr, "vkCreateInstance"));
  vk::ApplicationInfo app{0, nullptr, "ncaabb10", 1, "ncaabb10", 1, 1u << 22};  // Vulkan 1.0
  vk::InstanceCreateInfo info{1, nullptr, 0, &app, 0, nullptr, 0, nullptr};
  vk::Instance instance = nullptr;
  if (!create || create(&info, nullptr, &instance) != 0 || !instance) {
    return out;
  }
  const auto destroy =
      reinterpret_cast<vk::DestroyInstance>(get_proc(instance, "vkDestroyInstance"));
  const auto enumerate = reinterpret_cast<vk::EnumeratePhysicalDevices>(
      get_proc(instance, "vkEnumeratePhysicalDevices"));
  const auto properties = reinterpret_cast<vk::GetPhysicalDeviceProperties>(
      get_proc(instance, "vkGetPhysicalDeviceProperties"));
  uint32_t count = 0;
  if (enumerate && properties && enumerate(instance, &count, nullptr) == 0 && count > 0) {
    std::vector<vk::PhysicalDevice> devices(count);
    if (enumerate(instance, &count, devices.data()) >= 0) {  // VK_INCOMPLETE is fine too.
      devices.resize(count);
      // Without a dedicated card preferred stays -1: the renderer chooses, weighing the features
      // it wants.
      for (uint32_t i = 0; i < count; ++i) {
        // The whole structure is about 800 bytes; give it room to spare.
        alignas(16) unsigned char buffer[4096] = {};
        properties(devices[i], buffer);
        vk::PropertiesStart start;
        std::memcpy(&start, buffer, sizeof(start));
        start.device_name[sizeof(start.device_name) - 1] = '\0';
        if (start.device_type == vk::kCpu) {
          continue;
        }
        if (start.device_type == vk::kDiscreteGpu && out.preferred < 0) {
          out.preferred = static_cast<int>(out.cards.size());
        }
        out.cards.push_back({start.device_name, static_cast<int>(i)});
      }
    }
  }
  if (destroy) {
    destroy(instance, nullptr);
  }
  return out;
}

// The Vulkan renderer tries a device it is given and falls back to choosing one itself.
bool CanRenderOn(int) { return true; }

#endif

}  // namespace

bool CanRender(const GraphicsCard& card) { return CanRenderOn(card.index); }

const GraphicsCards& ListGraphicsCards() {
  static GraphicsCards cards;
  static std::once_flag once;
  std::call_once(once, [] {
    const auto start = std::chrono::steady_clock::now();
    cards = List();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    REXLOG_INFO("ncaabb10: {} graphics cards ({} ms):", cards.api, ms);
    for (size_t i = 0; i < cards.cards.size(); ++i) {
      REXLOG_INFO("ncaabb10:   {} = {}{}", cards.cards[i].index, cards.cards[i].name,
                  static_cast<int>(i) == cards.preferred ? " (preferred)" : "");
    }
  });
  return cards;
}

}  // namespace ncaabb10
