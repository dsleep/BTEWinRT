// BLEScanner.cpp : Scans for a Magic Blue BLE bulb, connects to it and sends it commands
//
// Copyright (C) 2016, Uri Shaked. License: MIT.
//
// ***
// See here for info about the bulb protocol: 
// https://medium.com/@urish/reverse-engineering-a-bluetooth-lightbulb-56580fcb7546
// ***
//

//#include "stdafx.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>

#include "combaseapi.h"

#include <iostream>
//
//#include <wrl/wrappers/corewrappers.h>
//#include <wrl/event.h>
//#include <collection.h>
//#include <ppltasks.h>

#include <string>
#include <sstream> 
#include <iomanip>
#include <set>

//#include <experimental/resumable>
//#include <pplawait.h>

#pragma comment(lib, "windowsapp")

using namespace winrt;
using namespace winrt::Windows::Devices;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;

auto serviceUUID = Bluetooth::BluetoothUuidHelper::FromShortId(0xffe5);
auto characteristicUUID = Bluetooth::BluetoothUuidHelper::FromShortId(0xffe9);

winrt::guid RequestedServiceGUID;
std::vector<winrt::guid> CharacteristicsToWatch;


std::wstring formatBluetoothAddress(unsigned long long BluetoothAddress) {
	std::wostringstream ret;
	ret << std::hex << std::setfill(L'0')
		<< std::setw(2) << ((BluetoothAddress >> (5 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((BluetoothAddress >> (4 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((BluetoothAddress >> (3 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((BluetoothAddress >> (2 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((BluetoothAddress >> (1 * 8)) & 0xff) << ":"
		<< std::setw(2) << ((BluetoothAddress >> (0 * 8)) & 0xff);
	return ret.str();
}
//
//concurrency::task<void> setColor(Bluetooth::GenericAttributeProfile::GattCharacteristic^ characteristic, byte red, byte green, byte blue) {
//	auto writer = ref new Windows::Storage::Streams::DataWriter();
//	auto data = new byte[7]{ 0x56, red, green, blue, 0x00, 0xf0, 0xaa };
//	writer->WriteBytes(ref new Array<byte>(data, 7));
//	auto status = co_await characteristic->WriteValueAsync(writer->DetachBuffer(), Bluetooth::GenericAttributeProfile::GattWriteOption::WriteWithoutResponse);
//	std::wcout << "Write result: " << status.ToString()->Data() << std::endl;
//}
//
//concurrency::task<void> connectToBulb(unsigned long long bluetoothAddress) {
//	auto leDevice = co_await Bluetooth::BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress);
//	auto servicesResult = co_await leDevice->GetGattServicesForUuidAsync(serviceUUID);
//	auto service = servicesResult->Services->GetAt(0);
//	auto characteristicsResult = co_await service->GetCharacteristicsForUuidAsync(characteristicUUID);
//	auto characteristic = characteristicsResult->Characteristics->GetAt(0);
//
//	co_await setColor(characteristic, 0, 0xff, 0); // Green
//
//	for (;;) {
//		Sleep(1000);
//		co_await setColor(characteristic, 0xff, 0xff, 0);	// Yellow
//
//		Sleep(1000);
//		co_await setColor(characteristic, 0xff, 0, 0);	// Red
//	}
//}

struct OurBTWatcher : winrt::implements<OurBTWatcher, IInspectable>
{
	winrt::Windows::Devices::Enumeration::DeviceWatcher deviceWatcher{ nullptr };
	winrt::Windows::Devices::Bluetooth::BluetoothLEDevice bluetoothLeDevice{ nullptr };


	std::vector<winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic> registeredCharacteristic;

	winrt::hstring m_value{ L"Hello, World!" };

	void Characteristic_ValueChanged(GattCharacteristic const& changed, GattValueChangedEventArgs args)
	{
		std::wcout << L"Characteristic_ValueChanged: " << std::endl;
	}

	fire_and_forget DeviceWatcher_Added(DeviceWatcher sender, DeviceInformation deviceInfo)
	{
		// We must update the collection on the UI thread because the collection is databound to a UI element.
		auto lifetime = get_strong();
		//co_await resume_foreground(Dispatcher());

		std::wcout <<  (L"DeviceWatcher_Added " + deviceInfo.Id() + deviceInfo.Name()).c_str() << std::endl;

		try
		{
			// BT_Code: BluetoothLEDevice.FromIdAsync must be called from a UI thread because it may prompt for consent.
			bluetoothLeDevice = co_await BluetoothLEDevice::FromIdAsync(deviceInfo.Id());

			if (bluetoothLeDevice == nullptr)
			{
				//rootPage.NotifyUser(L"Failed to connect to device.", NotifyType::ErrorMessage);
			}
		}
		catch (hresult_error& ex)
		{
			//if (ex.to_abi() == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_AVAILABLE))
			//{
			//	//rootPage.NotifyUser(L"Bluetooth radio is not on.", NotifyType::ErrorMessage);
			//}
			//else
			//{
			//	throw;
			//}
		}

		if (bluetoothLeDevice != nullptr)
		{
			// Note: BluetoothLEDevice.GattServices property will return an empty list for unpaired devices. For all uses we recommend using the GetGattServicesAsync method.
			// BT_Code: GetGattServicesAsync returns a list of all the supported services of the device (even if it's not paired to the system).
			// If the services supported by the device are expected to change during BT usage, subscribe to the GattServicesChanged event.
			GattDeviceServicesResult result = co_await bluetoothLeDevice.GetGattServicesAsync(BluetoothCacheMode::Uncached);

			if (result.Status() == GattCommunicationStatus::Success)
			{
				IVectorView<GattDeviceService> services = result.Services();
				//rootPage.NotifyUser(L"Found " + to_hstring(services.Size()) + L" services", NotifyType::StatusMessage);
				for (auto&& service : services)
				{
					guid uuid = service.Uuid();
					auto UUIStrign = to_hstring(uuid);
					std::wcout << L"Service" << UUIStrign.c_str() << std::endl;

					if (RequestedServiceGUID != uuid)
					{
						continue;
					}

					std::wcout << L"FOUND Service!" << std::endl;

					IVectorView<GattCharacteristic> characteristics{ nullptr };
					try
					{
						// Ensure we have access to the device.
						auto accessStatus = co_await service.RequestAccessAsync();
						if (accessStatus == DeviceAccessStatus::Allowed)
						{
							// BT_Code: Get all the child characteristics of a service. Use the cache mode to specify uncached characterstics only 
							// and the new Async functions to get the characteristics of unpaired devices as well. 
							GattCharacteristicsResult result = co_await service.GetCharacteristicsAsync(BluetoothCacheMode::Uncached);
							if (result.Status() == GattCommunicationStatus::Success)
							{
								characteristics = result.Characteristics();
							}
							else
							{
								//rootPage.NotifyUser(L"Error accessing service.", NotifyType::ErrorMessage);
							}
						}
						else
						{
							// Not granted access
							//rootPage.NotifyUser(L"Error accessing service.", NotifyType::ErrorMessage);
						}
					}
					catch (hresult_error& ex)
					{
						//rootPage.NotifyUser(L"Restricted service. Can't read characteristics: " + ex.message(), NotifyType::ErrorMessage);
					}

					if (characteristics)
					{
						for (GattCharacteristic&& c : characteristics)
						{
							guid uuid = c.Uuid();
							auto UUIStrign = to_hstring(uuid);
							std::wcout << L" - Characteristic: " << UUIStrign.c_str() << std::endl;
							
							auto foundIt = std::find(CharacteristicsToWatch.begin(), CharacteristicsToWatch.end(), uuid);
							if (foundIt == CharacteristicsToWatch.end())
							{
								continue;
							}
														
							if (std::find(registeredCharacteristic.begin(), registeredCharacteristic.end(), c) != registeredCharacteristic.end())
							{
								continue;
							}

							registeredCharacteristic.push_back(c);

							GattClientCharacteristicConfigurationDescriptorValue cccdValue = GattClientCharacteristicConfigurationDescriptorValue::None;
							if ((c.CharacteristicProperties() & GattCharacteristicProperties::Indicate) != GattCharacteristicProperties::None)
							{
								cccdValue = GattClientCharacteristicConfigurationDescriptorValue::Indicate;
							}

							else if ((c.CharacteristicProperties() & GattCharacteristicProperties::Notify) != GattCharacteristicProperties::None)
							{
								cccdValue = GattClientCharacteristicConfigurationDescriptorValue::Notify;
							}

							try
							{
								// BT_Code: Must write the CCCD in order for server to send indications.
								// We receive them in the ValueChanged event handler.
								GattCommunicationStatus status = co_await c.WriteClientCharacteristicConfigurationDescriptorAsync(cccdValue);

								if (status == GattCommunicationStatus::Success)
								{
									c.ValueChanged({ get_weak(), &OurBTWatcher::Characteristic_ValueChanged });

									//AddValueChangedHandler();
									//rootPage.NotifyUser(L"Successfully subscribed for value changes", NotifyType::StatusMessage);
								}
								else
								{
									//rootPage.NotifyUser(L"Error registering for value changes: Status = " + to_hstring(status), NotifyType::ErrorMessage);
								}
							}
							catch (hresult_access_denied& ex)
							{
								// This usually happens when a device reports that it support indicate, but it actually doesn't.
								//rootPage.NotifyUser(ex.message(), NotifyType::ErrorMessage);
							}

							//ComboBoxItem item;
							//item.Content(box_value(DisplayHelpers::GetCharacteristicName(c)));
							//item.Tag(c);
							//CharacteristicList().Items().Append(item);
						}
					}

					//ComboBoxItem item;
					//item.Content(box_value(DisplayHelpers::GetServiceName(service)));
					//item.Tag(service);
					//ServiceList().Items().Append(item);
				}
				//ConnectButton().Visibility(Visibility::Collapsed);
				//ServiceList().Visibility(Visibility::Visible);
			}
			else
			{
				//rootPage.NotifyUser(L"Device unreachable", NotifyType::ErrorMessage);
			}
		}

		// Protect against race condition if the task runs after the app stopped the deviceWatcher.
		//if (sender == deviceWatcher)
		//{
		//	// Make sure device isn't already present in the list.
		//	if (std::get<0>(FindBluetoothLEDeviceDisplay(deviceInfo.Id())) == nullptr)
		//	{
		//		if (!deviceInfo.Name().empty())
		//		{
		//			// If device has a friendly name display it immediately.
		//			m_knownDevices.Append(make<BluetoothLEDeviceDisplay>(deviceInfo));
		//		}
		//		else
		//		{
		//			// Add it to a list in case the name gets updated later. 
		//			UnknownDevices.push_back(deviceInfo);
		//		}
		//	}
		//}

		//co_return;
	}

	void DeviceWatcher_Updated(DeviceWatcher sender, DeviceInformationUpdate deviceInfoUpdate)
	{
		std::wcout << (L"DeviceWatcher_Updated" + deviceInfoUpdate.Id()).c_str() << std::endl;

	}

	void DeviceWatcher_Removed(DeviceWatcher sender, DeviceInformationUpdate deviceInfoUpdate)
	{
		std::wcout << (L"DeviceWatcher_Removed " + deviceInfoUpdate.Id()).c_str() << std::endl;

	}

	void DeviceWatcher_EnumerationCompleted(DeviceWatcher sender, IInspectable const&)
	{
		std::wcout << "DeviceWatcher_EnumerationCompleted" << std::endl;

	}

	void DeviceWatcher_Stopped(DeviceWatcher sender, IInspectable const&)
	{
		std::wcout << "DeviceWatcher_Stopped" << std::endl;
	}
};


int main(Platform::Array<Platform::String^>^ args)
{
	winrt::init_apartment();


	//GUID can be constructed from "{xxx....}" string using CLSID
	CLSIDFromString(L"{366DEE95-85A3-41C1-A507-8C3E02342000}", (LPCLSID)&RequestedServiceGUID);

	winrt::guid newCharToWatch;
	CLSIDFromString(L"{366DEE95-85A3-41C1-A507-8C3E02342001}", (LPCLSID)&newCharToWatch);
	CharacteristicsToWatch.push_back(newCharToWatch);
	//CLSIDFromString(L"{366DEE95-85A3-41C1-A507-8C3E02342002}", (LPCLSID)&RequestedServiceGUID);

	//Step 1: find the BLE device handle from its GUID
	//Microsoft::WRL::Wrappers::RoInitializeWrapper initialize(RO_INIT_MULTITHREADED);

	//CoInitializeSecurity(
	//	nullptr, // TODO: "O:BAG:BAD:(A;;0x7;;;PS)(A;;0x3;;;SY)(A;;0x7;;;BA)(A;;0x3;;;AC)(A;;0x3;;;LS)(A;;0x3;;;NS)"
	//	-1,
	//	nullptr,
	//	nullptr,
	//	RPC_C_AUTHN_LEVEL_DEFAULT,
	//	RPC_C_IMP_LEVEL_IDENTIFY,
	//	NULL,
	//	EOAC_NONE,
	//	nullptr);

#if 1
	// Additional properties we would like about the device.
		// Property strings are documented here https://msdn.microsoft.com/en-us/library/windows/desktop/ff521659(v=vs.85).aspx

	auto requestedProperties = single_threaded_vector<hstring>({ 
		L"System.Devices.Aep.DeviceAddress", 
		L"System.Devices.Aep.IsConnected", 
		L"System.Devices.Aep.Bluetooth.Le.IsConnectable" });

	auto watcherClass{ winrt::make_self<OurBTWatcher>() };
		
	// BT_Code: Example showing paired and non-paired in a single query.
	std::wstring aqsAllBluetoothLEDevices = L"(System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\")";

	auto deviceWatcher =
		winrt::Windows::Devices::Enumeration::DeviceInformation::CreateWatcher(
			aqsAllBluetoothLEDevices,
			requestedProperties,
			DeviceInformationKind::AssociationEndpoint);

	auto deviceWatcherAddedToken = deviceWatcher.Added({ watcherClass.get(), &OurBTWatcher::DeviceWatcher_Added });
	auto deviceWatcherUpdatedToken = deviceWatcher.Updated({ watcherClass.get(), &OurBTWatcher::DeviceWatcher_Updated });
	auto deviceWatcherRemovedToken = deviceWatcher.Removed({ watcherClass.get(), &OurBTWatcher::DeviceWatcher_Removed });
	auto deviceWatcherEnumerationCompletedToken = deviceWatcher.EnumerationCompleted({ watcherClass.get(), &OurBTWatcher::DeviceWatcher_EnumerationCompleted });
	auto deviceWatcherStoppedToken = deviceWatcher.Stopped({ watcherClass.get(), &OurBTWatcher::DeviceWatcher_Stopped });

	// Start the watcher. Active enumeration is limited to approximately 30 seconds.
	   // This limits power usage and reduces interference with other Bluetooth activities.
	   // To monitor for the presence of Bluetooth LE devices for an extended period,
	   // use the BluetoothLEAdvertisementWatcher runtime class. See the BluetoothAdvertisement
	   // sample for an example.
	deviceWatcher.Start();

#else

	Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^ bleAdvertisementWatcher = ref new Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher();
	bleAdvertisementWatcher->ScanningMode = Bluetooth::Advertisement::BluetoothLEScanningMode::Active;
	bleAdvertisementWatcher->Received += ref new Windows::Foundation::TypedEventHandler<Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^,
		Windows::Devices::Bluetooth::Advertisement::BluetoothLEAdvertisementReceivedEventArgs^>(
			[bleAdvertisementWatcher](Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^ watcher,
				Bluetooth::Advertisement::BluetoothLEAdvertisementReceivedEventArgs^ eventArgs)
			{
				auto serviceUuids = eventArgs->Advertisement->ServiceUuids;
				//unsigned int index = -1;
				//if (serviceUuids->IndexOf(serviceUUID, &index))
				{
					String^ strAddress = ref new String(formatBluetoothAddress(eventArgs->BluetoothAddress).c_str());
					std::wcout << "Target service found on device: " << strAddress->Data() << std::endl;

					//bleAdvertisementWatcher->Stop();

					//connectToBulb(eventArgs->BluetoothAddress);
				}
			});
	bleAdvertisementWatcher->Start();
#endif
	int a;
	std::cin >> a;
	return 0;
}
