/*
 * DeviceContext.cpp
 *****************************************************************************
 * Copyright (C) 2011 - 2013 Alpen-Adria-Universitšt Klagenfurt
 *
 * Created on: April 9, 2011
 * Authors: Benjamin Rainer <benjamin.rainer@itec.aau.at>
 *
 * This file is part of ambientLib.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/


#include "StdAfx.h"
#include <stdlib.h>
#include <direct.h>
#include <Windows.h>

#include "debug.h"
#include "ptr.h"
#include <iostream>
#include <fstream>
#include <vector>
#include "Element.h"
#include "DeviceInterface.h"
#include "Utils.h"
#include "DeviceContext.h"

using namespace std;


DeviceContext *DC = NULL;
static int ref_count = 0;


/*
 * ISSUE: this reference count holds only for one instance of the library, if there are more instances of this library are active than the device driver MUST HANDLE THIS PROBLEM !
 * THE ISSUE OF ACCESSING ONE DEVICE MORE THAN ONCE IS GIVEN TO THE DEVICE DRIVER... because we won't make this libaray static
 */

DeviceContext *GetDeviceContext(){

	if(ref_count == 0 && DC == NULL){
		ref_count++;
		DC = new DeviceContext();
	
		return DC;
	}

	if(DC != NULL){

		ref_count++;
		return DC;
	}
	return NULL;
}

DeviceContext *GetDeviceContext(bool disableFateTime,std::vector<Element *> elements){

	if(ref_count == 0 && DC == NULL){
		ref_count++;
		DC = new DeviceContext(disableFateTime, elements);
	
		return DC;
	}

	if(DC != NULL){

		ref_count++;
		return DC;
	}
	return NULL;
}

DeviceContext *GetDeviceContext(bool disableFateTime){

	if(ref_count == 0 && DC == NULL){
		ref_count++;
		DC = new DeviceContext(disableFateTime);
	
		return DC;
	}

	if(DC != NULL){

		ref_count++;
		return DC;
	}
	return NULL;
}


void ReleaseDeviceContext(){

	ref_count--;
	if(ref_count == 0 && DC != NULL){
		
		delete DC;
		DC = NULL;
		return;
	}
	
}

DeviceContext::DeviceContext()
{
	HINSTANCE drv;
	ctor_c_InitDeviceDriver InitDeviceDriverPtr;
	last_timestamp = 0.0;
	// read in the csv file and parse it, then load the standard device driver and initialize it
	// the csv file is called plugins.csv
	errorMessage = DEVICE_NO_ERROR;
	std::string line, all("");
	std::string envPath;
	std::string plgsPath;

	ifstream plugins;

	// Before we get the path out of the registry, we try to get the current working directory

	char workingDir[_MAX_PATH];
	getcwd(workingDir,_MAX_PATH);
	
	// try to open plugin.csv from the current working directory, if there is no such file fallback and use the path from the registry, which was set at the installation

	envPath = workingDir;

	for(unsigned int i=0;i<envPath.length();i++)
	{
		if (envPath[i] == '\\'){
			envPath.insert(i,1,'\\');
			i++;
		}
	}


	plgsPath = envPath;
	plgsPath.append("\\\\plugins.csv");
	plugins.open(plgsPath.c_str(),ios::out |ios::binary);
	if(plugins.fail())
	{
		// we fallback and try the registry entry
	

		// NOTE: the installer of the lib, or the developer has to SET THE environment variable AMBIENT to the path where the lib is
		// this is because the library can be used by a variety of programs and every program has it's own working directory
		char *en;
		size_t si;
		errno_t err = _dupenv_s(&en,&si,"AMBIENT");
		if(err) return;		// nothing loaded, no available registry path
		envPath = en;
		// convert to nt path
	
		for(unsigned int i=0;i<envPath.length();i++)
		{
			if (envPath[i] == '\\'){
				envPath.insert(i,1,'\\');
				i++;
			}
		}
	
		plgsPath = envPath;
		plgsPath.append("\\\\plugins.csv");
		plugins.open(plgsPath.c_str(),ios::out | ios::binary);
	}

	if(plugins.fail())
	{
		cout << "Error opening:"<< plgsPath << endl;
		MessageBox(NULL,plgsPath.c_str(),"Ambient Library - Error", MB_OK);
		errorMessage = DEVICE_ERROR_OPEN_DRIVER_LIST ;
	}else
	{
		while(plugins.good()){

			getline(plugins,line);
			all.append(line);
		}
	}
	plugins.close();

	// parse the file

	std::vector<std::string> devs = Utils::StringSplit(all,";");

	// now we know which drivers to load, because the number after every dll specifies which ones we are going to load

	// after loading the dll, query it and save which effects it can handle

	for(unsigned int i=0;i<devs.size();i+=2){

		if(atoi(devs[i+1].c_str()) == LOAD_DRIVER)
		{

			wchar_t *libText;
			std::string pt = envPath;
			pt.append("\\\\plugins\\\\");
			pt.append(devs[i]);
			libText = new wchar_t[strlen(pt.c_str())+1];
			memset(libText,0,strlen(pt.c_str())+1);
			MultiByteToWideChar(CP_ACP,NULL,pt.c_str(),-1,libText,strlen(pt.c_str())+1);
			
			drv = LoadLibrary(pt.c_str());
			delete []libText;
		
			if(drv != NULL){
				
				// dll got loaded properly

				// get our entrypoint
				InitDeviceDriverPtr = (ctor_c_InitDeviceDriver) GetProcAddress(drv,"ctor_c_InitDeviceDriver");

				if (NULL != InitDeviceDriverPtr)
				{
					// add the driver to our list
					DeviceInterface *devI = InitDeviceDriverPtr();	

					ptr<std::vector<int>> vec;				// we are responsible for the vector ...

					vec = new std::vector<int>();			

					devI->getSupportedSensoryEffects(vec);

					devices.insert(pair<DeviceInterface *,ptr<std::vector<int>>>(devI,vec));
					// now we have our driver loaded
			    	cout << "Loaded device driver successfuly!"<< endl;
					
					loadedPlugins.push_back(drv);
					
				}else{
					cout << "Error at loading driver: "<< devs[i].c_str() << endl;
					errorMessage = DEVICE_ERROR_LOADING_DRIVERS;
					FreeLibrary(drv);
				}

			}else{
				cout << "No such device driver could be found: " << devs[i].c_str() << endl;
			}

		}
	}
}

DeviceContext::DeviceContext(bool disableFateTime, std::vector<Element*>elements)
{
	HINSTANCE drv;
	ctor_a_InitDeviceDriver InitDeviceDriverPtr;
	last_timestamp = 0.0;
	// read in the csv file and parse it, then load the standard device driver and initialize it
	// the csv file is called plugins.csv
	errorMessage = DEVICE_NO_ERROR;
	std::string line, all("");
	std::string envPath;
	std::string plgsPath;
	ifstream plugins;
	
	// Before we get the path out of the registry, we try to get the current working directory
	DBG_MSG_W(NULL, "loading drivers", "Ambient Library - Debug Device Context Path", MB_OK);
	char workingDir[_MAX_PATH];
	getcwd(workingDir,_MAX_PATH);
	
	// try to open plugin.csv from the current working directory, if there is no such file fallback and use the path from the registry, which was set at the installation

	envPath = workingDir;

	for(unsigned int i=0;i<envPath.length();i++)
	{
		if (envPath[i] == '\\'){
			envPath.insert(i,1,'\\');
			i++;
		}
	}


	plgsPath = envPath;
	
	plgsPath.append("\\\\plugins.csv");

	plugins.open(plgsPath.c_str(),ios::out |ios::binary);
	
	if(plugins.fail())
	{
		// we fallback and try the registry entry
	

		// NOTE: the installer of the lib, or the developer has to SET THE environment variable AMBIENT to the path where the lib is
		// this is because the library can be used by a variety of programs and every program has it's own working directory
		char *en;
		size_t si;
		errno_t err = _dupenv_s(&en,&si,"AMBIENT");
		if(err) return;		// nothing loaded, no available registry path
		envPath = en;
		// convert to nt path
	
		for(unsigned int i=0;i<envPath.length();i++)
		{
			if (envPath[i] == '\\'){
				envPath.insert(i,1,'\\');
				i++;
			}
		}
	
		plgsPath = envPath;
		
		plgsPath.append("\\\\plugins.csv");
		plugins.open(plgsPath.c_str(),ios::out | ios::binary);
	}
	MessageBox(NULL, plgsPath.c_str(), "Ambient Library - Debug Device Context Path", MB_OK);
	if(plugins.fail())
	{
		
		cout << "Error opening:"<< plgsPath << endl;
		MessageBox(NULL,plgsPath.c_str(),"Ambient Library - Error", MB_OK);
		errorMessage = DEVICE_ERROR_OPEN_DRIVER_LIST ;
	}else
	{
		while(plugins.good()){

			getline(plugins,line);
			all.append(line);
		}
	}
	plugins.close();

	// parse the file

	std::vector<std::string> devs = Utils::StringSplit(all,";");

	// now we know which drivers to load, because the number after every dll specifies which ones we are going to load

	// after loading the dll, query it and save which effects it can handle

	for(unsigned int i=0;i<devs.size();i+=2){

		if(atoi(devs[i+1].c_str()) == LOAD_DRIVER)
		{

			wchar_t *libText;
			std::string pt = envPath;
			pt.append("\\\\plugins\\\\");
			pt.append(devs[i]);
			libText = new wchar_t[strlen(pt.c_str())+1];
			memset(libText,0,strlen(pt.c_str())+1);
			MultiByteToWideChar(CP_ACP,NULL,pt.c_str(),-1,libText,strlen(pt.c_str())+1);
			
			drv = LoadLibrary(pt.c_str());
			delete []libText;
		
			if(drv != NULL){
				
				// dll got loaded properly

				// get our entrypoint
				InitDeviceDriverPtr = (ctor_a_InitDeviceDriver) GetProcAddress(drv,"ctor_a_InitDeviceDriver");

				if (NULL != InitDeviceDriverPtr)
				{
					// add the driver to our list
					DeviceInterface *devI = InitDeviceDriverPtr(disableFateTime, elements);	

					ptr<std::vector<int>> vec;				// we are responsible for the vector ...

					vec = new std::vector<int>();			

					devI->getSupportedSensoryEffects(vec);

					devices.insert(pair<DeviceInterface *,ptr<std::vector<int>>>(devI,vec));
					// now we have our driver loaded
			    	cout << "Loaded device driver successfuly!"<< endl;
					
					loadedPlugins.push_back(drv);
					
				}else{
					cout << "Error at loading driver: "<< devs[i].c_str() << endl;
					errorMessage = DEVICE_ERROR_LOADING_DRIVERS;
					FreeLibrary(drv);
				}

			}else{
				cout << "No such device driver could be found: " << devs[i].c_str() << endl;
			}

		}
	}
}

DeviceContext::DeviceContext(bool disableFateTime)
{
	HINSTANCE drv;
	ctor_b_InitDeviceDriver InitDeviceDriverPtr;
	last_timestamp = 0.0;
	// read in the csv file and parse it, then load the standard device driver and initialize it
	// the csv file is called plugins.csv
	errorMessage = DEVICE_NO_ERROR;
	std::string line, all("");
	std::string envPath;
	std::string plgsPath;

	ifstream plugins;

	// Before we get the path out of the registry, we try to get the current working directory

	char workingDir[_MAX_PATH];
	getcwd(workingDir,_MAX_PATH);
	
	// try to open plugin.csv from the current working directory, if there is no such file fallback and use the path from the registry, which was set at the installation

	envPath = workingDir;

	for(unsigned int i=0;i<envPath.length();i++)
	{
		if (envPath[i] == '\\'){
			envPath.insert(i,1,'\\');
			i++;
		}
	}


	plgsPath = envPath;
	plgsPath.append("\\\\plugins.csv");
	plugins.open(plgsPath.c_str(),ios::out |ios::binary);
	if(plugins.fail())
	{
		// we fallback and try the registry entry
	

		// NOTE: the installer of the lib, or the developer has to SET THE environment variable AMBIENT to the path where the lib is
		// this is because the library can be used by a variety of programs and every program has it's own working directory
		char *en;
		size_t si;
		errno_t err = _dupenv_s(&en,&si,"AMBIENT");
		if(err) return;		// nothing loaded, no available registry path
		envPath = en;
		// convert to nt path
	
		for(unsigned int i=0;i<envPath.length();i++)
		{
			if (envPath[i] == '\\'){
				envPath.insert(i,1,'\\');
				i++;
			}
		}
	
		plgsPath = envPath;
	
		plgsPath.append("\\\\plugins.csv");
		plugins.open(plgsPath.c_str(),ios::out | ios::binary);
	}

	if(plugins.fail())
	{
		cout << "Error opening:"<< plgsPath << endl;
		MessageBox(NULL,plgsPath.c_str(),"Ambient Library - Error", MB_OK);
		errorMessage = DEVICE_ERROR_OPEN_DRIVER_LIST ;
	}else
	{
		while(plugins.good()){

			getline(plugins,line);
			all.append(line);
		}
	}
	plugins.close();

	// parse the file
	//MessageBox(NULL,all.c_str(), "MUH", MB_OK);
	std::vector<std::string> devs = Utils::StringSplit(all,";");
	
	// now we know which drivers to load, because the number after every dll specifies which ones we are going to load

	// after loading the dll, query it and save which effects it can handle

	for(unsigned int i=0;i<devs.size();i+=2){
	
		if(atoi(devs[i+1].c_str()) == LOAD_DRIVER)
		{
			
			wchar_t *libText;
			std::string pt = envPath;
			pt.append("\\\\plugins\\\\");
			
			
			pt.append(devs[i].c_str());
			
			libText = new wchar_t[strlen(pt.c_str())+1];
			memset(libText,0,strlen(pt.c_str())+1);
			MultiByteToWideChar(CP_ACP,NULL,pt.c_str(),-1,libText,strlen(pt.c_str())+1);
			
			drv = LoadLibrary(pt.c_str());
			delete []libText;
		
			if(drv != NULL){
				
				// dll got loaded properly

				// get our entrypoint
				InitDeviceDriverPtr = (ctor_b_InitDeviceDriver) GetProcAddress(drv,"ctor_b_InitDeviceDriver");

				if (NULL != InitDeviceDriverPtr)
				{
					// add the driver to our list
					DeviceInterface *devI = InitDeviceDriverPtr(disableFateTime);	

					ptr<std::vector<int>> vec;				// we are responsible for the vector ...

					vec = new std::vector<int>();			

					devI->getSupportedSensoryEffects(vec);

					devices.insert(pair<DeviceInterface *,ptr<std::vector<int>>>(devI,vec));
					// now we have our driver loaded
			    	cout << "Loaded device driver successfuly!"<< endl;
					
					loadedPlugins.push_back(drv);
					
				}else{
					std::string error_msg;
					error_msg.append("Error at loading driver: ");
					error_msg.append(devs[i].c_str());
					MessageBox(NULL, error_msg.c_str(), "Ambient Library - Error", MB_OK);
					cout << "Error at loading driver: "<< devs[i].c_str() << endl;
					errorMessage = DEVICE_ERROR_LOADING_DRIVERS;
					FreeLibrary(drv);
				}

			}else{
				cout << "No such device driver could be found: " << devs[i].c_str() << endl;
			}

		}else{

			//MessageBox(NULL, devs[i].c_str(), "Not Loading..", MB_OK);
		}
	}
}


DeviceContext::~DeviceContext(void)
{
		
	// if we are deleted, delete all loaded drivers
	std::map<DeviceInterface *,ptr<std::vector<int>>>::iterator it;
	std::vector<HINSTANCE>::iterator dit;


	for(it = devices.begin(); it != devices.end(); ++it){
		// free the module
		delete	(*it).first;
		// the second is freed by the std itself, deleting the vector we have created here, will lead to an heap corruption
		delete (*it).second.get_pointer();
	}

	devices.clear();
	
	// now unload the dlls

	for(dit = loadedPlugins.begin(); dit != loadedPlugins.end(); ++dit){

		FreeLibrary((*dit));

	}

	loadedPlugins.clear();
	
	// we are out of business

}

int DeviceContext::getErrorMessage()
{
	return errorMessage;

}

// this adds all the defined effects to all devices
int DeviceContext::AddEffectsToDevices(std::vector<Element *> elements){
	int n=0;
	
	std::map<DeviceInterface *,ptr<std::vector<int>>>::iterator it;
	
	
	for(it = devices.begin(); it != devices.end(); ++it)
	{

		(*it).first->addEffects(elements);	// hand over all effect elements to every single device, may be one can understand it :)
		n++;
	}


	return n;		// how many devices have received our effect elements ?
}


// fetch all errors from the devices, this just fetches the most recent error codes from all devices attached
std::vector<std::string> DeviceContext::getDeviceErrors()
{
	std::map<DeviceInterface *, ptr<std::vector<int>>>::iterator it;
	std::vector<std::string> errorCodes;
	
	for(it = devices.begin(); it != devices.end(); ++it) {
		if(it->first->hasError()) {
			errorCodes.push_back(it->first->getErrorString());
			it->first->clearError();
		}
	}
	return errorCodes;

}


// tell all drivers the length of the video
void DeviceContext::setVideoLength(double length)
{
	std::map<DeviceInterface *, ptr<std::vector<int>>>::iterator it;
	for(it = devices.begin(); it != devices.end(); ++it)
	{
		it->first->setVideoLength(length);
	}

}

// again to all devices
bool DeviceContext::playEffectAtTime(double time){

	bool played = false;
	std::map<DeviceInterface *,ptr<std::vector<int>>>::iterator it;
	
	
	for(it = devices.begin(); it != devices.end(); ++it)
	{

		if(!played)	played = (*it).first->playEffectAtTime(time);
		else
			(*it).first->playEffectAtTime(time); // one effect was played but there may be other effects at the same timestamp 

	}

	// save this timestamp
	last_timestamp = time;

	return played;		
}

void DeviceContext::colorToDevice(std::string loc, int r, int g, int b)
{
	std::vector<int>::iterator vit;
	std::map<DeviceInterface *, ptr<std::vector<int>>>::iterator it;

	for(it = devices.begin(); it != devices.end(); ++it)
	{
		for(vit = (*it).second->begin(); vit != (*it).second->end(); ++vit)
		{

			if((*vit) == DEVICE_SUPPORTS_LIGHTS) (*it).first->colorToDevice(loc,r,g,b);

		}
	}

}

void DeviceContext::devicesDisplayColor()
{
	std::vector<int>::iterator vit;
	std::map<DeviceInterface *,ptr<std::vector<int>>>::iterator it;

	for(it = devices.begin(); it != devices.end(); ++it){
		
		for(vit = (*it).second->begin(); vit != (*it).second->end(); ++vit)
		{
			if((*vit) == DEVICE_SUPPORTS_LIGHTS) (*it).first->displayColor();
		}
	}
}


void DeviceContext::activateAllAvailableDevices(bool activate)
{
	std::map<DeviceInterface *,ptr<std::vector<int>>>::iterator it;

	for(it = devices.begin(); it != devices.end(); ++it)
	{
		(*it).first->activateAllAvailableDevices(activate);
	}
}

void DeviceContext::activateDevice(unsigned int DEVICE, bool activate)
{
	std::map<DeviceInterface *, ptr<std::vector<int>>>::iterator it;
	std::vector<int>::iterator vit;
	


	for(it = devices.begin(); it!=devices.end(); ++it)
	{
		for(vit = it->second->begin(); vit != it->second->end(); ++vit)
		{

			if((*vit) == DEVICE) it->first->activateDevice(DEVICE, activate);

		}

	}

}

void DeviceContext::activateAutoColor(bool activate)
{
	std::vector<int>::iterator vit;
	std::map<DeviceInterface *,ptr<std::vector<int>>>::iterator it;

	for(it = devices.begin(); it != devices.end(); ++it){
		
		for(vit = (*it).second->begin(); vit != (*it).second->end(); ++vit)
		{
			if((*vit) == DEVICE_SUPPORTS_LIGHTS) (*it).first->activateAutoColor(activate);
		}
	}

}
