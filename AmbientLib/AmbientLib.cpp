/*
 * AmbientLib.cpp
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


#include "stdafx.h"
#include <iostream>
#include <vector>

#include "debug.h"

#include "PreReader.h"
#include "AmbientLib.h"
#include "Libxml.h"
#include "ptr.h"

#include "Element.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "HTTPConnector.h"
#include "DeviceInterface.h"
#include "DeviceContext.h"
#include "ScreenSplitter.h"
#include "workerPool.h"
#include "colorAlgorithm.h"
#include "SEMhttp.h"
#include "Utils.h"

#ifdef DEBUG
#define new DEBUG_NEW
#endif

using namespace AmbientLib;

Ambient::Ambient(bool allowThreads,bool disableFateTime,bool enableAutoColor, unsigned int width, unsigned int height)
{
	
	ptr<DeviceContext> dc;
	DBG_MSG_W("init device context", "AmbientLib");
	dc = GetDeviceContext(disableFateTime);
	DBG_MSG_W("done with device context", "AmbientLib");
	this->width = width;
	this->height = height;
	this->allowThreads = allowThreads;
	if(enableAutoColor)
	{
		autoColor = true;
		ptr<ScreenSplitter> splitter(new ScreenSplitter(width, height));
		dc->activateAutoColor(true);
		cA = new colorAlgorithm(dc,splitter, allowThreads);
	}else{
		autoColor = false;
		cA = NULL;
	}
	dc->activateAllAvailableDevices(true);
	
	myDC = dc.get_pointer();
	pts = 0;
	effectsLoaded = false;
}

int Ambient::getDeviceContextErrorMessage()
{

	if(myDC != NULL) {

		ptr<DeviceContext> dc(myDC);

		return dc->getErrorMessage();

	}else{

		return 0;
	}
}

int Ambient::getDeviceErrorMessages(pair &apair)
{
	if(myDC != NULL)
	{
		ptr<DeviceContext> dc(myDC);

		std::vector<std::string> errorMsgs = dc->getDeviceErrors();
		
		// i know this is not really nice, but it will do it's work
		std::vector<std::string>::iterator it;
		if(errorMsgs.size() > 0 && errorMsgs.size() <= MAX_ERROR_MSGS) 
		{
			for(unsigned int i=0;i<errorMsgs.size(); i++)
			{

				apair.string[i] = errorMsgs[i].c_str();

			}
			apair.size = errorMsgs.size();

		}else
		{
			apair.size = 0;
		}

	}else
	{
		apair.size = 0;
	}
	return 0;
}

int Ambient::loadSEMHTTP(const char *_url,const char *_SEMFile, bool directoryLookup){
	
	std::string url(_url);
	std::string SEMFile(_SEMFile);
	ptr<SEMhttp> semLoader(new SEMhttp(url,SEMFile));
	ptr<Libxml> xml(new Libxml());
	ptr<DeviceContext> dc(myDC);
	if(directoryLookup)
	{
		// do some error checking 
		std::vector<std::string> list = semLoader->loadSEMList();
		if(list.size() == 0) {

			delete xml.get_pointer();
			delete semLoader.get_pointer();
			timeScale = 0;			// no timescale, no loaded SEM
			return 1;
		}

		std::string sem = semLoader->fetchFirstSEM(list);

		if(sem.size() == 0){

			// no files there ...
			
			delete xml.get_pointer();
			delete semLoader.get_pointer();
			timeScale = 0;			// no timescale, no loaded SEM
			return 1;

		}
		std::vector<Element *> elements = xml->parse(sem);

		if(elements.size() == 0)
		{
			// we have found an sem file but it's empty, or invalid
			delete xml.get_pointer();
			delete semLoader.get_pointer();
			timeScale = 0;			// no timescale, no loaded SEM
			return 1;
		}
		
		dc->AddEffectsToDevices(elements);

	}else{

		url.append(SEMFile);
		// load the sem file without directory lookup
		std::string sem = semLoader->loadSEM(SEMFile);

		if(sem.size() == 0){

			// no files there ...
			
			delete xml.get_pointer();
			delete semLoader.get_pointer();
			timeScale = 0;			// no timescale, no loaded SEM
			return 1;

		}

		std::vector<Element *> elements = xml->parse(sem);

		if(elements.size() == 0)
		{
			// we have found an sem file but it's empty, or invalid
			delete xml.get_pointer();
			delete semLoader.get_pointer();
			timeScale = 0;			// no timescale, no loaded SEM
			return 1;
		}

		dc->AddEffectsToDevices(elements);

	}

	timeScale = xml->getTimeScale();

	if(xml->isAutoColorActivated() && !autoColor)
	{
		activateAutoColor();
	}
	else
	{
		if(!xml->isAutoColorActivated()) dc->activateAutoColor(false);
		else
			dc->activateAutoColor(true);
	}

	autoColor = xml->isAutoColorActivated();
	
	pts = 0;
	effectsLoaded = true;
	delete xml.get_pointer();
	delete semLoader.get_pointer();

	return 0;

}

int Ambient::loadSEMFromDisk(const char *SEMFile){
	ptr<Libxml> xml(new Libxml());
	ptr<DeviceContext> dc(myDC);

	std::vector<Element *> elements = xml->parse(SEMFile);

	if(elements.size() == 0)
	{
		// we have found an sem file but it's empty, or invalid
		delete xml.get_pointer();
		timeScale = 0;			// no timescale, no loaded SEM
		return 1;
	}
	
	dc->AddEffectsToDevices(elements);

	timeScale = xml->getTimeScale();
	
	
	if(xml->isAutoColorActivated() && !autoColor)
	{
		activateAutoColor();
	}
	else
	{
		if(!xml->isAutoColorActivated())if(cA != NULL) delete cA;
		else
			dc->activateAutoColor(true);
		
	}
	// check if we should use the auto color calculation
	autoColor = xml->isAutoColorActivated();
	effectsLoaded = true;
	delete xml.get_pointer();
	
	pts = 0;
	return 0;
}

int Ambient::loadWithPreLoader(const char *file){

	// just use the semFile as preReader file ...
	ptr<PreReader> preR(new PreReader());
	ptr<DeviceContext> dc(myDC);
	dc->AddEffectsToDevices(preR->parse(file));

	timeScale = preR->getTimeScale();

	autoColor = false;
	delete cA;					// standard enabled per constructor
	
	cA = NULL;	// no automatic color calculation when we use the preReader
	pts = 0;

	effectsLoaded = true;

	delete preR.get_pointer();
	return 0;
}

int Ambient::activateAutoColor()
{
	ptr<DeviceContext> dc(myDC);
	if(cA == NULL)
	{
		autoColor = true;
		ptr<ScreenSplitter> splitter(new ScreenSplitter(width, height));
		dc->activateAutoColor(true);
		cA = new colorAlgorithm(dc,splitter, allowThreads);
	}

	return 0;

}

int Ambient::setCACalculationRate(int pixelJump, int rowJump){

	ptr<colorAlgorithm> p(cA);
	p->setCalculationRate(pixelJump,rowJump);

	return 0;
}

int Ambient::lightFromPicture(unsigned char *data, int PIXEL_FORMAT)
{

	// just forward the call
	if(cA == NULL || !autoColor) return -1;	// no automatic color calculation available
	ptr<colorAlgorithm> p(cA);
	return p->doCalculation((float) width, (float) height, data, PIXEL_FORMAT);
}

bool Ambient::playEffectAtTimeAbs(double TS_ABS){
	if(!effectsLoaded) return false;
	ptr<DeviceContext> p(myDC);
	bool ret = p->playEffectAtTime(Utils::Round(TS_ABS,1));
	pts = TS_ABS;
	return ret; // DEF
}

bool Ambient::playEffectAtTimeRel(double TS_OFF){
	if(!effectsLoaded) return false;
	pts+= TS_OFF;
	ptr<DeviceContext> p(myDC);
	bool ret = p->playEffectAtTime(Utils::Round(pts,1));

	return ret; // DEF
}


int Ambient::reset()
{
	// sometimes we have just to reset our state, for example if one plays a movie twice with the same options ..
	pts = 0;
	return 0;
}

bool Ambient::isEffectsLoaded()
{
	return effectsLoaded;
}

bool Ambient::isAutoColor(){return autoColor;}

unsigned long Ambient::getTimeScale(){return timeScale;}

int Ambient::activateDevice(unsigned int DEVICE, bool activate)
{
	ptr<DeviceContext> dc(myDC);
	dc->activateDevice(DEVICE, activate);
	return 0;
}

int Ambient::activateAllDevices(bool activate)
{
	ptr<DeviceContext> dc(myDC);
	dc->activateAllAvailableDevices(activate);
	return 0;
}

int Ambient::setVideoLength(double length)
{
	ptr<DeviceContext> dc(myDC);
	dc->setVideoLength(length);
	return 0;
}

Ambient::~Ambient(){
	
	// delete all effects we handed to the device and release the context
	if (cA != NULL) delete ((colorAlgorithm *)cA);
	ReleaseDeviceContext();
}
