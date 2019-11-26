#ifndef W_SETTINGS_H
#define W_SETTINGS_H

#include "EEPROM.h"
#include "WProperty.h"

const byte STORED_FLAG = 0x15;
const int EEPROM_SIZE = 512;

class WSettingItem {
public:
	WProperty* value;
	int address;
	WSettingItem* next = nullptr;
};

class WSettings {
public:
	WSettings(bool debug) {
		this->debug = debug;
		EEPROM.begin(EEPROM_SIZE);
		this->_existsSettings = (EEPROM.read(0) == STORED_FLAG);
		EEPROM.end();
	}

	void save() {
		EEPROM.begin(EEPROM_SIZE);
		WSettingItem* settingItem = firstSetting;
		while (settingItem != nullptr) {
			WProperty* setting = settingItem->value;
			switch (setting->getType()){
			case BOOLEAN:
				EEPROM.write(settingItem->address, (setting->getBoolean() ? 0xFF : 0x00));
				break;
			case BYTE:
				EEPROM.write(settingItem->address, setting->getByte());
				break;
			case INTEGER:
				byte low, high;
				low = (setting->getInteger() & 0xFF);
				high = ((setting->getInteger()>>8) & 0xFF);
				EEPROM.write(settingItem->address, low);
				EEPROM.write(settingItem->address + 1, high);
				break;
			case DOUBLE:
				EEPROM.put(settingItem->address, setting->getDouble());
				break;
			case STRING:
				writeString(settingItem->address, setting->getLength(), setting->getString());
				break;
			}
			settingItem = settingItem->next;
		}
		//1. Byte - settingsStored flag
		EEPROM.write(0, STORED_FLAG);
		EEPROM.commit();
		EEPROM.end();
	}



	bool existsSetting(String id) {
		return (getSetting(id) != nullptr);
	}

	bool existsSettings() {
		return this->_existsSettings;
	}

	WProperty* getSetting(String id) {
		WSettingItem* settingItem = firstSetting;
		while (settingItem != nullptr) {
			WProperty* setting = settingItem->value;
			if (id.equals(setting->getId())) {
				return setting;
			}
			settingItem = settingItem->next;
		}
		return nullptr;
	}

	void add(WProperty* property) {
		if (getSetting(property->getId()) == nullptr) {
			WSettingItem* settingItem = addSetting(property);
			if (existsSettings()) {
				EEPROM.begin(EEPROM_SIZE);
				switch (property->getType()) {
				case BOOLEAN:
					property->setBoolean(EEPROM.read(settingItem->address) == 0xFF);
					break;
				case DOUBLE:
					double d;
					EEPROM.get(settingItem->address, d);
					property->setDouble(d);
					break;
				case INTEGER:
					byte low, high;
					low = EEPROM.read(settingItem->address);
					high = EEPROM.read(settingItem->address + 1);
					property->setInteger(low + ((high << 8)&0xFF00));
					break;
				case BYTE:
					property->setByte(EEPROM.read(settingItem->address));
					break;
				case STRING:
					String rs = readString(settingItem->address, property->getLength());
					property->setString(rs);
					break;
				}
				EEPROM.end();
			}
		}
	}

	WProperty* registerBoolean(String id, bool defaultValue) {
		WProperty* setting = getSetting(id);
		if (setting == nullptr) {
			setting = new WProperty(id, "", "", BOOLEAN);
			//setting->length = 1;
			WSettingItem* settingItem = addSetting(setting);
			if (existsSettings()) {
				EEPROM.begin(EEPROM_SIZE);
				setting->setBoolean(EEPROM.read(settingItem->address) == 0xFF);
				EEPROM.end();
			} else {
				setting->setBoolean(defaultValue);
			}
		}
		return setting;
	}

	bool getBoolean(String id) {
		WProperty* setting = getSetting(id);
		return (setting != nullptr ? setting->getBoolean() : false);
	}

	void setBoolean(String id, bool value) {
		WProperty* setting = registerBoolean(id, value);
		setting->setBoolean(value);
	}

	WProperty* registerByte(String id, byte defaultValue) {
		WProperty* setting = getSetting(id);
		if (setting == nullptr) {
			setting = new WProperty(id, "", "", BYTE);
			//setting->length = 1;
			WSettingItem* settingItem = addSetting(setting);
			if (existsSettings()) {
				EEPROM.begin(EEPROM_SIZE);
				setting->setByte(EEPROM.read(settingItem->address));
				EEPROM.end();
			} else {
				setting->setByte(defaultValue);
			}
		}
		return setting;
	}

	byte getByte(String id) {
		WProperty* setting = getSetting(id);
		return (setting != nullptr ? setting->getByte() : 0x00);
	}

	void setByte(String id, byte value) {
		WProperty* setting = registerByte(id, value);
		setting->setByte(value);
	}

	WProperty* registerInteger(String id, int defaultValue) {
		WProperty* setting = getSetting(id);
		if (setting == nullptr) {
			setting = new WProperty(id, "", "", INTEGER);
			//setting->length = 2;
			WSettingItem* settingItem = addSetting(setting);
			if (existsSettings()) {
				EEPROM.begin(EEPROM_SIZE);
				byte low, high;
				low = EEPROM.read(settingItem->address);
				high = EEPROM.read(settingItem->address + 1);
				setting->setInteger(low + ((high << 8)&0xFF00));
				EEPROM.end();
			} else {
				setting->setInteger(defaultValue);
			}
		}
		return setting;
	}

	int getInteger(String id) {
		WProperty* setting = getSetting(id);
		return (setting != nullptr ? setting->getInteger() : 0);
	}

	void setInteger(String id, int value) {
		WProperty* setting = registerInteger(id, value);
		setting->setInteger(value);
	}

	WProperty* registerDouble(String id, double defaultValue) {
		WProperty* setting = getSetting(id);
		if (setting == nullptr) {
			setting = new WProperty(id, "", "", DOUBLE);
			WSettingItem* settingItem = addSetting(setting);
			if (existsSettings()) {
				EEPROM.begin(EEPROM_SIZE);
				double d;
				EEPROM.get(settingItem->address, d);
				EEPROM.end();
				setting->setDouble(d);
			} else {
				setting->setDouble(defaultValue);
			}
		}
		return setting;
	}

	double getDouble(String id) {
		WProperty* setting = getSetting(id);
		return (setting != nullptr ? setting->getDouble() : 0.0);
	}

	void setDouble(String id, double value) {
		WProperty* setting = registerDouble(id, value);
		setting->setDouble(value);
	}

	WProperty* registerString(String id, byte length, String defaultValue) {
		WProperty* setting = getSetting(id);
		if (setting == nullptr) {
			setting = new WProperty(id, "", "", STRING, length);
			//setting->length = length + 1;
			WSettingItem* settingItem = addSetting(setting);
			if (existsSettings()) {
				EEPROM.begin(EEPROM_SIZE);
				String rs = readString(settingItem->address, setting->getLength());
				setting->setString(rs);
				EEPROM.end();
			} else {
				setting->setString(defaultValue);
			}
		}
		return setting;
	}

	String getString(String id) {
		WProperty* setting = getSetting(id);
		return (setting != nullptr ? setting->getString() : "");
	}

	void setString(String id, String value) {
		WProperty* setting = registerString(id, 32, value);
		setting->setString(value);
	}

protected:
	WSettingItem* addSetting(WProperty* setting) {
		WSettingItem* settingItem = new WSettingItem();
		settingItem->value = setting;
		if (this->lastSetting == nullptr) {
			settingItem->address = 1;
			this->firstSetting = settingItem;
			this->lastSetting = settingItem;
		} else {
			settingItem->address = this->lastSetting->address + this->lastSetting->value->getLength();
			this->lastSetting->next = settingItem;
			this->lastSetting = settingItem;
		}
		return settingItem;
	}
private:
	bool debug;
	bool _existsSettings;
	WSettingItem* firstSetting = nullptr;
	WSettingItem* lastSetting = nullptr;

	String readString(int address, int length) {
		char data[length]; //Max 100 Bytes
		for (int i = 0; i < length; i++) {
			byte k = EEPROM.read(address + i);
			data[i] = k;
			if (k == '\0') {
				break;
			}
		}
		return String(data);
	}

	void writeString(int address, int length, String value) {
		int size = value.length();
		if (size + 1 >= length) {
			size = length - 1;
		}
		for (int i = 0; i < size; i++) {
			EEPROM.write(address + i, value[i]);
		}
		EEPROM.write(address + size, '\0');
	}

	void log(String debugMessage) {
		if (debug) {
			Serial.println(debugMessage);
		}
	}

};

#endif