/*
 * coloAlgorithm.cpp
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
#include <Windows.h>
#include "Element.h"
#include "ptr.h"
#include "DeviceInterface.h"
#include "DeviceContext.h"
#include "threading.h"
#include "cpuFeatures.h"
#include "ScreenSplitter.h"
#include "workerPool.h"
#include "colorAlgorithm.h"
#include <cassert>

using namespace AmbientLib;
using namespace std;
colorAlgorithm::colorAlgorithm(ptr<DeviceContext> dc,ptr<ScreenSplitter> splitter, bool allowThreads){

	this->DC = dc;
	this->screenSplitter = splitter;
	this->allowThreads = allowThreads;		// check if we are allowed to use threading
	// set the default calculation rate
	this->setCalculationRate(DEFAULT_CALCULATION_PIXEL_JUMP, DEFAULT_CALCULATION_ROW_JUMP);

	if(allowThreads){
		
		wPool = new workerPool();

		// okay here we create our thread, the event to signal that there is data and the mutex for the data
		this->m_ThreadMutex = CreateMutex(NULL, FALSE, NULL);
		this->m_ScreenSplitterMutex = CreateMutex(NULL, FALSE, NULL);
		this->m_ThreadEvent = CreateEvent(NULL, TRUE, TRUE, TEXT("EventForSignalingData"));
		this->m_ThreadLocksDone = CreateEvent(NULL, TRUE, TRUE, TEXT("EventForSignalingLocks"));
		ResetEvent(this->m_ThreadEvent); // reset our event
		ResetEvent(this->m_ThreadLocksDone);
		// now our thread
		// give the thread a pointer to our class so that we can access the event and the mutex
		// we won't have all the data at this point, so we just give him what we have
		this->tData.cA = this;

		this->m_Thread = CreateThread(NULL,0,(LPTHREAD_START_ROUTINE) &ThreadFunc,(LPVOID)&tData,0,&dwThreadId);
	}

	// check whether the cpu supports MMX and all other features we may need for a fast color calculation

	CPU::cpuFeatures *cpuF = new CPU::cpuFeatures();
	
	// if MMX is there then we may use MMX to speed up color calculation!
	cpuFeaturesSupported = cpuF->isFeatureSupported(CPU::cpuFeatures::CPU_FEATURE_MMX);
	useCPUFeatures = cpuFeaturesSupported;
	delete cpuF; // we won't need the features anymore


}

colorAlgorithm::~colorAlgorithm(){
	// delete the event, the mutex and kill the thread, in ZOMBIE state there may occur no memory leaks if we kill the thread
	if(allowThreads){
		
		// before we kill this thread, we wait until the thread has nothing to do
		WaitForSingleObject(this->m_ThreadEvent,1);
		if (wPool.get_pointer() != NULL) delete wPool.get_pointer();
		// first kill the thread, because we don't want to get null pointer exceptions on the event if the thread is waiting on it (and he will wait for the event :))
		TerminateThread(this->m_Thread,0); // NOTICE: Win2k & Win2003 Server may cause resource leaks... (USE msgs)
		ReleaseMutex(this->m_ThreadMutex);
		CloseHandle(this->m_ThreadEvent);
		CloseHandle(this->m_ThreadLocksDone);
		CloseHandle(this->m_ScreenSplitterMutex);
	}

	delete screenSplitter.get_pointer();

}

bool colorAlgorithm::haveCPUSupport() { return cpuFeaturesSupported; }
bool colorAlgorithm::usingCPUSupport() { return useCPUFeatures;}

bool colorAlgorithm::forceCPUSupport(bool set){

	if(cpuFeaturesSupported){

		useCPUFeatures = set;
		return true; // for sure ...
	}

	if(!cpuFeaturesSupported){

		// enabling mmx would crash the app, so just SAY NO

		return false;

	}

	return false;

}

void colorAlgorithm::addFourComponentDataMMX(unsigned int currentX, unsigned char *rawData,components *cps){

	unsigned int c1,c2,c3,c4;

	__asm{
				push eax
				push ebx
				
				pxor mm0,mm0
				pxor mm1,mm1
				pxor mm2,mm2
				pxor mm3,mm3
				pxor mm4,mm4
				
				mov eax,rawData
				add eax,currentX
				movd mm1, dword ptr [eax]
				add eax,4
				movd mm2, dword ptr [eax]
				add eax,4
				movd mm3, dword ptr [eax]
				add eax,4
				movd mm4, dword ptr [eax]


				punpcklbw mm1,mm0
				punpcklbw mm2,mm0
				punpcklbw mm3,mm0
				punpcklbw mm4,mm0

				paddw mm1,mm2
				paddw mm1,mm3
				paddw mm1,mm4



				movd ebx,mm1
				and ebx,0xFFFF
				mov c1,ebx

				movd ebx,mm1
				shr ebx,16
				and ebx,0xFFFF
				mov c2,ebx

				psrlq mm1,32
				movd ebx,mm1
				and ebx,0xFFFF
				mov c3,ebx
				
				psrlq mm1,16
				movd ebx,mm1
				and ebx, 0xFFFF
				mov c4, ebx

				pop ebx
				pop eax
				emms 

			}

	*cps->c1 = c1;
	*cps->c2 = c2;
	*cps->c3 = c3;
	*cps->c4 = c4;
}

void colorAlgorithm::addThreeComponentDataMMX(unsigned int currentX, unsigned char *rawData,components *cps){

	unsigned int c1,c2,c3;


	__asm{
				push eax
				push ebx
				
				pxor mm0,mm0
				pxor mm1,mm1
				pxor mm2,mm2
				pxor mm3,mm3
				pxor mm4,mm4

				mov eax,rawData
				add eax,currentX

				// due to the register size we read 4 components but we actualy have only 3 components so just omit the fourth at the extraction
				movd mm1, dword ptr [eax]			// ex: RGB R
				add eax,3
				movd mm2, dword ptr [eax]			// ex: RGB R
				add eax,3
				movd mm3, dword ptr [eax]			// ...
				add eax,3
				movd mm4, dword ptr [eax]


				punpcklbw mm1,mm0
				punpcklbw mm2,mm0
				punpcklbw mm3,mm0
				punpcklbw mm4,mm0

				paddw mm1,mm2
				paddw mm1,mm3
				paddw mm1,mm4



				movd ebx,mm1
				and ebx,0xFFFF
				mov c1,ebx

				movd ebx,mm1
				shr ebx,16
				and ebx,0xFFFF
				mov c2,ebx

				psrlq mm1,32
				movd ebx,mm1
				and ebx,0xFFFF
				mov c3,ebx
				

				pop ebx
				pop eax
				emms 

			}
	*cps->c1 = c1;
	*cps->c2 = c2;
	*cps->c3 = c3;

}

/*
 * Threaded versions of the average color algorithm
 */

int colorAlgorithm::_t_calcRGBA(unsigned long stride,unsigned int startHeight, unsigned int endHeight,unsigned int **container, unsigned char *rawData)
{

	unsigned short colorR=0, colorG=0,colorB=0,alpha=0;
	components cps;
	cps.c1 = &colorR;
	cps.c2 = &colorG;
	cps.c3 = &colorB;
	cps.c4 = &alpha;

	
	// we have to divide our image in 3 parts on the x lane
	int split = (stride/4)/3; // yep, we may miss 1 pixel (is 1 pixel so significant :) ?)

	if(useCPUFeatures){

		for(unsigned long y = startHeight;y < endHeight; y += (rowJump + 1))
		{
			
			for(unsigned long x = 0;x < stride/4; x +=(pixelJump*4) + 4)
			{			

				unsigned int currentX = (stride * y)+x*4;
		
				addFourComponentDataMMX(currentX,rawData,&cps);
			
				unsigned int cur_loc = screenSplitter->getLocation(x,y);
				container[cur_loc][0] += colorR;
				container[cur_loc][1] += colorG;
				container[cur_loc][2] += colorB;
				container[cur_loc][3] += alpha;
				container[cur_loc][4] += 4;
					
			}
		}

	}else{

		for(unsigned long y = startHeight;y < endHeight; y += (this->rowJump + 1)){
		
			for(unsigned long x = 0;x < stride; x += (this->pixelJump*4) + 1){
				colorR = rawData[(stride*y)+x];
				x++;
				colorG = rawData[(stride*y)+x];
				x++;
				colorB = rawData[(stride*y)+x];
				x++;
				// last but not least
				alpha = rawData[(stride*y)+x];
				unsigned int cur_loc = screenSplitter->getLocation(((x+1)/4)-1,y);
				container[cur_loc][0] += colorR;
				container[cur_loc][1] += colorG;
				container[cur_loc][2] += colorB;
				container[cur_loc][3] += alpha;
				container[cur_loc][4] ++;

			}
		}

	}
	
	return 0;
}

int colorAlgorithm::_t_calcBGRA(unsigned long stride,unsigned int startHeight, unsigned int endHeight,unsigned int **container, unsigned char *rawData)
{
	unsigned short colorR=0, colorG=0,colorB=0,alpha=0;
	components cps;
	cps.c1 = &colorB;
	cps.c2 = &colorG;
	cps.c3 = &colorR;
	cps.c4 = &alpha;



	// we have to divide our image in 3 parts on the x lane
	int split = (stride/4)/3; // yep, we may miss 1 pixel (is 1 pixel so significant :) ?)

	if(useCPUFeatures){

		for(unsigned long y = startHeight;y < endHeight; y += (rowJump + 1))
		{
			for(unsigned long x = 0;x < stride/4; x +=(pixelJump*4) + 4)
			{			

				unsigned int currentX = (stride * y)+x*4;
		
				addFourComponentDataMMX(currentX,rawData,&cps);
			
				unsigned int cur_loc = screenSplitter->getLocation(x,y);
				container[cur_loc][0] += colorR;
				container[cur_loc][1] += colorG;
				container[cur_loc][2] += colorB;
				container[cur_loc][3] += alpha;
				container[cur_loc][4] += 4;
					
			}
		}

	}else{

		for(unsigned long y = startHeight;y < endHeight; y += (this->rowJump + 1))
		{
			for(unsigned long x = 0;x < stride; x += (this->pixelJump*4) + 1)
			{
				colorB = rawData[(stride*y)+x];
				x++;
				colorG = rawData[(stride*y)+x];
				x++;
				colorR = rawData[(stride*y)+x];
				x++;
				// last but not least
				alpha = rawData[(stride*y)+x];
				unsigned int cur_loc = screenSplitter->getLocation(((x+1)/4)-1,y);
				container[cur_loc][0] += colorR;
				container[cur_loc][1] += colorG;
				container[cur_loc][2] += colorB;
				container[cur_loc][3] += alpha;
				container[cur_loc][4] ++;

			}
		}

	}
	return 0;
}

int colorAlgorithm::_t_calcARGB(unsigned long stride,unsigned int startHeight, unsigned int endHeight,unsigned int **container, unsigned char *rawData)
{
	unsigned short colorR=0, colorG=0,colorB=0,alpha=0;
	components cps;
	cps.c1 = &alpha;
	cps.c2 = &colorR;
	cps.c3 = &colorG;
	cps.c4 = &colorB;

	// we have to divide our image in 3 parts on the x lane
	int split = (stride/4)/3; // yep, we may miss 1 pixel (is 1 pixel so significant :) ?)

	if(useCPUFeatures){

		for(unsigned long y = startHeight;y < endHeight; y += (rowJump + 1))
		{
			

			for(unsigned long x = 0;x < stride/4; x +=(pixelJump*4) + 4)
			{			

				unsigned int currentX = (stride * y)+x*4;
		
				addFourComponentDataMMX(currentX,rawData,&cps);
			
				unsigned int cur_loc = screenSplitter->getLocation(x,y);
				container[cur_loc][0] += colorR;
				container[cur_loc][1] += colorG;
				container[cur_loc][2] += colorB;
				container[cur_loc][3] += alpha;
				container[cur_loc][4] += 4;
					
			}
		}

	}else{

		for(unsigned long y = startHeight;y < endHeight; y += (this->rowJump + 1)){
			for(unsigned long x = 0;x < stride; x += (this->pixelJump*4) + 1){
				alpha = rawData[(stride*y)+x];
				x++;
				colorR = rawData[(stride*y)+x];
				x++;
				colorG = rawData[(stride*y)+x];
				x++;
				// last but not least
				colorB = rawData[(stride*y)+x];
				unsigned int cur_loc = screenSplitter->getLocation(((x+1)/4)-1,y);
				container[cur_loc][0] += colorR;
				container[cur_loc][1] += colorG;
				container[cur_loc][2] += colorB;
				container[cur_loc][3] += alpha;
				container[cur_loc][4] ++;

			}
		}

	}
	return 0;
}

int colorAlgorithm::_t_calcRGB(unsigned long stride,unsigned int startHeight, unsigned int endHeight,unsigned int **container, unsigned char *rawData)
{
	unsigned short colorR=0, colorG=0,colorB=0,alpha=0;
	components cps;
	cps.c1 = &colorR;
	cps.c2 = &colorG;
	cps.c3 = &colorB;


	// we have to divide our image in 3 parts on the x lane
	int split = (stride/3)/3; // yep, we may miss 1 pixel (is 1 pixel so significant :) ?)

	if(useCPUFeatures){

		for(unsigned long y = startHeight;y < endHeight; y += (rowJump + 1))
		{
			
			for(unsigned long x = 0;x < stride/3; x +=(pixelJump*4) + 4)
			{			

				unsigned int currentX = (stride * y)+x*3;
		
				addThreeComponentDataMMX(currentX,rawData,&cps);
			
				unsigned int cur_loc = screenSplitter->getLocation(x,y);
				container[cur_loc][0] += colorR;
				container[cur_loc][1] += colorG;
				container[cur_loc][2] += colorB;
				container[cur_loc][3] = 0;
				container[cur_loc][4] += 4;
					
			}
		}

	}else{

		for(unsigned long y = startHeight;y < endHeight; y += (this->rowJump + 1)){
			for(unsigned long x = 0;x < stride; x += (this->pixelJump*4) + 1){
				colorR = rawData[(stride*y)+x];
				x++;
				colorG = rawData[(stride*y)+x];
				x++;
				colorB = rawData[(stride*y)+x];
				
				unsigned int cur_loc = screenSplitter->getLocation(((x+1)/3)-1,y);
				container[cur_loc][0] += colorR;
				container[cur_loc][1] += colorG;
				container[cur_loc][2] += colorB;
				container[cur_loc][3] = 0;
				container[cur_loc][4] ++;

			}
		}

	}
	return 0;
}

int colorAlgorithm::_t_calcBGR(unsigned long stride,unsigned int startHeight, unsigned int endHeight,unsigned int **container, unsigned char *rawData)
{

	unsigned short colorR=0, colorG=0,colorB=0,alpha=0;
	components cps;
	cps.c1 = &colorB;
	cps.c2 = &colorG;
	cps.c3 = &colorR;


	// we have to divide our image in 3 parts on the x lane
	int split = (stride/3)/3; // yep, we may miss 1 pixel (is 1 pixel so significant :) ?)

	if(useCPUFeatures){

		for(unsigned long y = startHeight;y < endHeight; y += (rowJump + 1))
		{
			
			for(unsigned long x = 0;x < stride/3; x +=(pixelJump*4) + 4)
			{			

				unsigned int currentX = (stride * y)+x*3;
		
				addThreeComponentDataMMX(currentX,rawData,&cps);
			
				unsigned int cur_loc = screenSplitter->getLocation(x,y);
				container[cur_loc][0] += colorR;
				container[cur_loc][1] += colorG;
				container[cur_loc][2] += colorB;
				container[cur_loc][3] = 0;
				container[cur_loc][4] += 4;
					
			}
		}

	}else{

		for(unsigned long y = startHeight;y < endHeight; y += (this->rowJump + 1)){
			for(unsigned long x = 0;x < stride; x += (this->pixelJump*4) + 1){
				colorB = rawData[(stride*y)+x];
				x++;
				colorG = rawData[(stride*y)+x];
				x++;
				colorR = rawData[(stride*y)+x];
				
				unsigned int cur_loc = screenSplitter->getLocation(((x+1)/3)-1,y);
				container[cur_loc][0] += colorR;
				container[cur_loc][1] += colorG;
				container[cur_loc][2] += colorB;
				container[cur_loc][3] = 0;
				container[cur_loc][4] ++;

			}
		}

	}
	return 0;
}

void colorAlgorithm::threadedCalculation(void *threadD, workerParam *wParam)
{
	threadData *tD = (threadData *) threadD;
	
	// create our own screenSplitter container to avoid unnecessary synchronization
	unsigned int **container;
	unsigned int locations = tD->cA->screenSplitter->getEnumLocations();
	container = (unsigned int **) malloc(sizeof(unsigned int *) * locations);
	for(unsigned int i=0; i<locations;i++)
	{
		container[i] = (unsigned int *) malloc(sizeof(unsigned int) * 5);
		memset(container[i], 0, sizeof(unsigned int) * 5);
	}
	
	// now check which part is done by us
	
	unsigned int startHeight = (unsigned int) floor(tD->height / wParam->workers) * wParam->threadID; 
    unsigned int endHeight = (unsigned int) floor(tD->height / wParam->workers) * (wParam->threadID+1);
	
	// now our calculation ...

	// check which format we have and run the appropriate function

	unsigned int stride = (unsigned int) tD->width * tD->cA->bytesPerFormat(tD->format);

	switch(tD->format)
	{
		case IMAGE_FORMAT_RGBA:
			tD->cA->_t_calcRGBA(stride,startHeight,endHeight,container,tD->data);
		break;

		case IMAGE_FORMAT_ARGB:
			tD->cA->_t_calcARGB(stride,startHeight,endHeight,container,tD->data);
		break;

		case IMAGE_FORMAT_BGRA:
			tD->cA->_t_calcBGRA(stride,startHeight,endHeight,container,tD->data);
		break;

		case IMAGE_FORMAT_BGR:
			tD->cA->_t_calcBGR(stride,startHeight,endHeight,container,tD->data);
		break;

		case IMAGE_FORMAT_RGB:
			tD->cA->_t_calcRGB(stride,startHeight,endHeight,container,tD->data);
		break;

	};


	// now add our results to the global results
	WaitForSingleObject(tD->cA->m_ScreenSplitterMutex,INFINITE);

	for(unsigned int i=0;i<locations;i++)
	{
		tD->cA->screenSplitter->insertAtLocation(i,container[i][0],container[i][1], container[i][2], container[i][3], container[i][4]);
	}

	ReleaseMutex(tD->cA->m_ScreenSplitterMutex);

	for(unsigned int i=0; i<locations;i++)
	{
		free(container[i]);
	}

	free(container);
	
}

/*
 * Non threaded versions of the average color algorithm
 */

int colorAlgorithm::calcRGB(long stride,float height, unsigned char *rawData){
	unsigned short colorR=0, colorG=0,colorB=0;
	int left[3],middle[3],right[3];
	int calcHeight=0,calcWidth=0;
	unsigned int pxCountLeft=0,pxCountMiddle=0,pxCountRight=0;
	screenSplitter->reset();
	for (int n=0;n<3;n++){
		left[n] = middle[n] = right[n] = 0;
	}

	components cps;
	cps.c1 = &colorR;
	cps.c2 = &colorG;
	cps.c3 = &colorB;


	// we have to divide our image in 3 parts on the x lane
	int split = (stride/3)/3; // yep, we may miss 1 pixel (is 1 pixel so significant :) ?)


	// here is a big room for optimization, the biggest would be threading this whole algorithm, may be in the next version :)
	// for now we go with this method
	if(useCPUFeatures){

		for(long y = 0;y < height; y += (this->rowJump + 1)){
				calcHeight++;
				for(long x = 0;x < stride/3; x +=(this->pixelJump*4) + 4){			

					// and here comes the magic, we add 4 pixels at once :)
					// MMX and SIMD are magical :X
					unsigned int currentX = (stride * y)+x*3;
			
					addThreeComponentDataMMX(currentX,rawData,&cps);

					screenSplitter->insert((unsigned int)x,(unsigned int)y,(unsigned int)colorR, (unsigned int)colorG, (unsigned int)colorB,(unsigned int)0, 4);


	
				}
		
		}


	}else{
		for(long y = 0;y < height; y += (this->rowJump + 1)){
			calcHeight++;
			for(long x = 0;x < stride; x += (this->pixelJump*3) + 1){
			
				colorR = rawData[(stride*y)+x];
				x++;
				colorG = rawData[(stride*y)+x];
				x++;
				colorB = rawData[(stride*y)+x];

				screenSplitter->insert((unsigned int)x,(unsigned int)y,(unsigned int)colorR, (unsigned int)colorG, (unsigned int)colorB,(unsigned int)0);

	
	
			}
		}

	
	}

	forwardColorToDevice();
	return 1;
	
}

int colorAlgorithm::calcRGBA(long stride,float height, unsigned char *rawData){
	
	unsigned short colorR=0, colorG=0,colorB=0,alpha=0;
	unsigned int diff=0;
	int left[4],middle[4],right[4];
	int calcHeight=0,calcWidth=0;
	unsigned int pxCountLeft=0,pxCountMiddle=0,pxCountRight=0;
	screenSplitter->reset();
	components cps;
	cps.c1 = &colorR;
	cps.c2 = &colorG;
	cps.c3 = &colorB;
	cps.c4 = &alpha;

	for (int n=0;n<4;n++){
		left[n] = middle[n] = right[n] = 0;
	}

	// we have to divide our image in 3 parts on the x lane
	int split = (stride/4)/3; // yep, we may miss 1 pixel (is 1 pixel so significant :) ?)
		
	// check for pixel jumps :x those px jumps can mess up the whole calculation
	// for now jump 4 * pxjump
	if(useCPUFeatures){

		for(long y = 0;y < height; y += (this->rowJump + 1)){
				calcHeight++;
				for(long x = 0;x < stride/4; x +=(this->pixelJump*4) + 4){			

					// and here comes the magic, we add 4 pixels at once :)
					// MMX and SIMD are magical :X
					unsigned int currentX = (stride * y)+x*4;
			
					addFourComponentDataMMX(currentX,rawData,&cps);
					
					screenSplitter->insert((unsigned int)x,(unsigned int)y,(unsigned int)colorR, (unsigned int)colorG, (unsigned int)colorB,(unsigned int)0, 4);

				}
		}

	}else{

		for(long y = 0;y < height; y += (this->rowJump + 1)){
			calcHeight++;
			for(long x = 0;x < stride; x += (this->pixelJump*4) + 1){
				colorR = rawData[(stride*y)+x];
				x++;
				colorG = rawData[(stride*y)+x];
				x++;
				colorB = rawData[(stride*y)+x];
				x++;
				// last but not least
				alpha = rawData[(stride*y)+x];
				screenSplitter->insert((unsigned int)x,(unsigned int)y,(unsigned int)colorR, (unsigned int)colorG, (unsigned int)colorB,(unsigned int)0);

			}
		}

	}

	forwardColorToDevice();
	return 1;
}

int colorAlgorithm::calcBGRA(long stride,float height, unsigned char *rawData){
	
	unsigned short colorR=0, colorG=0,colorB=0,alpha=0;
	unsigned int diff=0;
	int left[4],middle[4],right[4];
	int calcHeight=0,calcWidth=0;
	unsigned int pxCountLeft=0,pxCountMiddle=0,pxCountRight=0;
	screenSplitter->reset();
	components cps;
	cps.c1 = &colorR;
	cps.c2 = &colorG;
	cps.c3 = &colorB;
	cps.c4 = &alpha;

	for (int n=0;n<4;n++){
		left[n] = middle[n] = right[n] = 0;
	}

	// we have to divide our image in 3 parts on the x lane
	int split = (stride/4)/3; // yep, we may miss 1 pixel (is 1 pixel so significant :) ?)
		
	// check for pixel jumps :x those px jumps can mess up the whole calculation
	// for now jump 4 * pxjump
	if(useCPUFeatures){

		for(long y = 0;y < height; y += (this->rowJump + 1)){
				calcHeight++;
				for(long x = 0;x < stride/4; x +=(this->pixelJump*4) + 4){			

					// and here comes the magic, we add 4 pixels at once :)
					// MMX and SIMD are magical :X
					unsigned int currentX = (stride * y)+x*4;
			
					addFourComponentDataMMX(currentX,rawData,&cps);
					
					screenSplitter->insert((unsigned int)x,(unsigned int)y,(unsigned int)colorB, (unsigned int)colorG, (unsigned int)colorR,(unsigned int)0, 4);

				}
		}

	}else{

		for(long y = 0;y < height; y += (this->rowJump + 1)){
			for(long x = 0;x < stride; x += (this->pixelJump*4) + 1){
				colorR = rawData[(stride*y)+x];
				x++;
				colorG = rawData[(stride*y)+x];
				x++;
				colorB = rawData[(stride*y)+x];
				x++;
				// last but not least
				alpha = rawData[(stride*y)+x];
				screenSplitter->insert((unsigned int)x,(unsigned int)y,(unsigned int)colorR, (unsigned int)colorG, (unsigned int)colorB,(unsigned int)0);


			}
		}

	}

	forwardColorToDevice();
	return 1;
}

int colorAlgorithm::calcARGB(long stride,float height, unsigned char *rawData){

	unsigned short colorR=0, colorG=0,colorB=0,alpha=0;
	int left[4],middle[4],right[4];
	int calcHeight=0,calcWidth=0;
	unsigned int pxCountLeft=0, pxCountMiddle=0,pxCountRight=0;
	components cps;
	screenSplitter->reset();
	cps.c1 = &alpha;
	cps.c2 = &colorR;
	cps.c3 = &colorG;
	cps.c4 = &colorB;

	for (int n=0;n<4;n++){
		left[n] = middle[n] = right[n] = 0;
	}

	// we have to divide our image in 3 parts on the x lane
	int split = (stride/4)/3; // yep, we may miss 1 pixel (is 1 pixel so significant :) ?)


	// here is a big room for optimization, the biggest would be threading this whole algorithm, may be in the next version :)
	// for now we go with this method

	if(useCPUFeatures){
		for(long y = 0;y < height; y += (this->rowJump + 1)){
				calcHeight++;
				for(long x = 0;x < stride/4; x +=(this->pixelJump*4) + 4){			

					// and here comes the magic, we add 4 pixels at once :)
					// MMX and SIMD are magical :X
					unsigned int currentX = (stride * y)+x*4;
			
					addFourComponentDataMMX(currentX,rawData,&cps);
					screenSplitter->insert((unsigned int)x,(unsigned int)y,(unsigned int)colorR, (unsigned int)colorG, (unsigned int)colorB,(unsigned int)alpha, 4);

				}
		}


	}else{

		for(long y = 0;y < height; y += (this->rowJump + 1)){
			calcHeight++;
			for(long x = 0;x < stride; x += (this->pixelJump*4) + 1){
			
				alpha = rawData[(stride*y)+x];
				x++;
				colorR = rawData[(stride*y)+x];
				x++;
				colorG = rawData[(stride*y)+x];
				x++;
				colorB = rawData[(stride*y)+x];
			
				screenSplitter->insert((unsigned int)x,(unsigned int)y,(unsigned int)colorR, (unsigned int)colorG, (unsigned int)colorB,(unsigned int)alpha);

			
			}
		}


	}
	forwardColorToDevice();
	return 1;
	
}

int colorAlgorithm::calcBGR(long stride,float height, unsigned char *rawData){
	unsigned short colorR=0, colorG=0,colorB=0;
	int left[3],middle[3],right[3];
	int calcHeight=0,calcWidth=0;
	unsigned int pxCountLeft=0, pxCountMiddle=0,pxCountRight=0;
	components cps;
	cps.c1 = &colorB;
	cps.c2 = &colorG;
	cps.c3 = &colorR;
	screenSplitter->reset();
	for (int n=0;n<3;n++){
		left[n] = middle[n] = right[n] = 0;
	}

	// we have to divide our image in 3 parts on the x lane
	int split = (stride/3)/3; // yep, we may miss 1 pixel (is 1 pixel so significant :) ?)


	// here is a big room for optimization, the biggest would be threading this whole algorithm, may be in the next version :)
	// for now we go with this method
	if(useCPUFeatures){
		for(long y = 0;y < height; y += (this->rowJump + 1)){
				calcHeight++;
				for(long x = 0;x < stride/3; x +=(this->pixelJump*4) + 4){			

					// and here comes the magic, we add 4 pixels at once :)
					// MMX and SIMD are magical :X
					unsigned int currentX = (stride * y)+x*3;
			
					addThreeComponentDataMMX(currentX,rawData,&cps);
					screenSplitter->insert((unsigned int)x,(unsigned int)y,(unsigned int)colorR, (unsigned int)colorG, (unsigned int)colorB,(unsigned int)0, 4);

				}
		}

	}else{

		for(long y = 0;y < height; y += (this->rowJump + 1)){
			calcHeight++;
			for(long x = 0;x < stride; x += (this->pixelJump*3) + 1){
				
				colorB = rawData[(stride*y)+x];
				x++;
				colorG = rawData[(stride*y)+x];
				x++;
				colorR = rawData[(stride*y)+x];
				screenSplitter->insert((unsigned int)x,(unsigned int)y,(unsigned int)colorR, (unsigned int)colorG, (unsigned int)colorB,0);

			}
		}
	
	}

	forwardColorToDevice();
	return 1;
}

void colorAlgorithm::forwardColorToDevice()
{
	// loop through all locations and hand over our color
	for(int i=1;i<DEFINED_LOCATIONS;i++)
	{
		DC->colorToDevice(screenSplitter->getMapping(i), screenSplitter->getLocationComponent(0,i),screenSplitter->getLocationComponent(1,i), screenSplitter->getLocationComponent(2,i));
	
	}
	DC->devicesDisplayColor();

}

void colorAlgorithm::clearLights(){

	
}

void colorAlgorithm::setCalculationRate(int pixelJump, int rowJump){
	
	this->pixelJump = pixelJump;
	this->rowJump = rowJump;

}

LARGE_INTEGER timerFreq_;
LARGE_INTEGER counterAtStart_;

void startTime()
{
  QueryPerformanceFrequency(&timerFreq_);
  QueryPerformanceCounter(&counterAtStart_);
  //cout<<"timerFreq_ = "<<timerFreq_.QuadPart<<endl;
  //cout<<"counterAtStart_ = "<<counterAtStart_.QuadPart<<endl;
  TIMECAPS ptc;
  UINT cbtc = 8;
  MMRESULT result = timeGetDevCaps(&ptc, cbtc);
  if (result == TIMERR_NOERROR)
  {
  
	//cout<<"Minimum resolution = "<<ptc.wPeriodMin<<endl;
    //cout<<"Maximum resolution = "<<ptc.wPeriodMax<<endl;
  }
  else
  {
    cout<<"result = TIMER ERROR"<<endl;
  }
}
unsigned int calculateElapsedTime()
{
  if (timerFreq_.QuadPart == 0)
  {
    return -1;
  }
  else
  {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<unsigned int>( (c.QuadPart - counterAtStart_.QuadPart) * 1000000 / timerFreq_.QuadPart );
  }
}

/*
 * Color calculation thread.
 * ok i should have made this inside the class but we may need this thread again under other circumstances
 */

DWORD ThreadFunc(LPVOID threadParams){

	threadData *myData = (threadData *)threadParams;
	colorAlgorithm *myCA = myData->cA;
	
	// now we can access the event and the mutex
	while(true){
	//	SetEvent(myCA->m_ThreadEvent);	// states that we are ready
	
		// check if the event is here
		WaitForSingleObject(myCA->m_ThreadLocksDone,INFINITE);	// wait forever
		// reset the event, we may wait for the next event
		ResetEvent(myCA->m_ThreadLocksDone);

		// ok the event is here, try to lock our data
		WaitForSingleObject(myCA->m_ThreadMutex,INFINITE);

		// ok everything we need is here, so just do the calc
		// before we do anything, reset our screen splitter
		
		/*timeBeginPeriod(1000000);
		startTime();
		unsigned int diff =0,ems,bms;
		for(int i=0;i<100;i++){
		bms = calculateElapsedTime();*/
		myCA->screenSplitter->reset();
		myCA->wPool->attachAndWait(colorAlgorithm::threadedCalculation,threadParams);
		/*ems = calculateElapsedTime();
		diff += ems-bms;}
		timeEndPeriod(1000000);
		std::cout << "color calculation took: " << diff/100 << " micros" << std::endl;*/
		myCA->forwardColorToDevice();
		// we have the task to free the data we processed
		free(myData->data);
		
		// unlock our mutex
		ReleaseMutex(myCA->m_ThreadMutex);

	}

}

int colorAlgorithm::doCalculation(float width,float height, unsigned char *rawData, int FORMAT){

	// calculate the dominant color
	long stride;
	

	switch(FORMAT){
	case IMAGE_FORMAT_RGB:
		stride = (long)(width * bytesPerFormat(FORMAT)); // 24 BIT (R, G, B)
		if(this->allowThreads) return this->calcColorThreaded(width,height,rawData,FORMAT);
		else
			return this->calcRGB(stride,height,rawData);
		break;
	case IMAGE_FORMAT_RGBA:
		stride = (long)(width * bytesPerFormat(FORMAT)); // 32 BIT (R, G, B, A)
		if(this->allowThreads) return this->calcColorThreaded(width,height,rawData,FORMAT);
		else
			return this->calcRGBA(stride,height,rawData);
		break;
	case IMAGE_FORMAT_ARGB:
		stride = (long)(width * bytesPerFormat(FORMAT)); // 32 BIT (A,R,G,B)
		if(this->allowThreads) return this->calcColorThreaded(width,height,rawData,FORMAT);
		else
			return this->calcARGB(stride,height,rawData);
		break;
	case IMAGE_FORMAT_BGR:
		stride = (long)(width * bytesPerFormat(FORMAT)); // 24 BIT (B,G,R)
		if(this->allowThreads) return this->calcColorThreaded(width,height,rawData,FORMAT);
		else
			return this->calcBGR(stride,height,rawData);
		break;
	case IMAGE_FORMAT_BGRA:
		stride = (long)(width * bytesPerFormat(FORMAT)); // 32 BIT (B,G,R.A)
		if(this->allowThreads) return this->calcColorThreaded(width,height,rawData,FORMAT);
		else
			return this->calcBGRA(stride,height,rawData);
	break;
	default:
		return IMAGE_INVALID_FORMAT;	// NYI / UNKNOWN 
	}
	


}

int colorAlgorithm::calcColor(float width, float height, unsigned char *rawData, int FORMAT){

	// calculate the dominant color
	long stride;

	switch(FORMAT){
	case IMAGE_FORMAT_RGB:
		stride = (long)(width * bytesPerFormat(FORMAT)); // 24 BIT (R, G, B)
		return this->calcRGB(stride,height,rawData);
		break;
	case IMAGE_FORMAT_RGBA:
		stride = (long)(width * bytesPerFormat(FORMAT)); // 32 BIT (R, G, B, A)
		return this->calcRGBA(stride,height,rawData);
		break;
	case IMAGE_FORMAT_ARGB:
		stride = (long)(width * bytesPerFormat(FORMAT)); // 32 BIT (A,R,G,B)
		return this->calcARGB(stride,height,rawData);
		break;
	case IMAGE_FORMAT_BGR:
		stride = (long)(width * bytesPerFormat(FORMAT)); // 24 BIT (B,G,R)
		return this->calcBGR(stride,height,rawData);
		break;
	case IMAGE_FORMAT_BGRA:
		stride = (long)(width * bytesPerFormat(FORMAT)); // 32 BIT (B,G,R,A)
		return this->calcBGRA(stride,height,rawData);
		break;
	default:
		return IMAGE_INVALID_FORMAT;	// NYI / UNKNOWN 
	}
	
	
}

int colorAlgorithm::bytesPerFormat(int FORMAT){

	switch(FORMAT){
	case IMAGE_FORMAT_RGB:
	case IMAGE_FORMAT_BGR:
		return 3; // 24 bit
		break;
	case IMAGE_FORMAT_BGRA:
	case IMAGE_FORMAT_RGBA:
	case IMAGE_FORMAT_ARGB:
		return 4; // 32 BIT 
		break;
	default:
		return IMAGE_INVALID_FORMAT;	// NYI / UNKNOWN 
	}

}

int colorAlgorithm::calcColorThreaded(float width, float height, unsigned char *rawData, int FORMAT){
	
	DWORD erc;
	
	assert(this->allowThreads);

	// we are willed to wait 1 ms, if it is not here we won't process the data
	erc = WaitForSingleObject(this->m_ThreadMutex,1);
	
	if(erc != WAIT_TIMEOUT){
	//	ResetEvent(this->m_ThreadEvent);
		// if we have no time left than lock it, fill our data, release the mutex, signal that there is work and go on
		this->tData.data = (unsigned char *) malloc((size_t)(height * width * bytesPerFormat(FORMAT)));
		memcpy(this->tData.data, rawData, (size_t)(height*width*bytesPerFormat(FORMAT)));
		this->tData.pixelJump = pixelJump;
		this->tData.rowJump = rowJump;
		this->tData.height = height;
		this->tData.width = width;
		this->tData.format = FORMAT;
		// okay release the data and signal it to the thread
		ReleaseMutex(this->m_ThreadMutex);
		SetEvent(this->m_ThreadLocksDone);
	//	WaitForSingleObject(this->m_ThreadLocksDone,INFINITE);
	//	ResetEvent(this->m_ThreadLocksDone);
		// okay, we're out of business
		return 5;
	}

	// if the thread is processing some data, we won't bother him, because we won't cause delays in the video, it smells like QoS :P
	return 4;

}