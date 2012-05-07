/*
 * Libxml.h
 *****************************************************************************
 * Copyright (C) 2011 - 2013 Alpen-Adria-Universit�t Klagenfurt
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

#ifndef LIBXML_H
#define LIBXML_H

#include <vector>
#include <string>
#include "Element.h"
#include "libxml/xmlreader.h"

class Libxml
{
private:
	void processNode(xmlTextReaderPtr reader);
	bool m_anchorElement;
	long m_timeScale;
	double m_curTime;
	Element *m_tmpElement;
	void streamFile(const char *filename);
	void streamFileFromMemory(std::string file);
	std::vector<Element*> *m_Elements;
	static bool elementCompare(const Element* element1, const Element* element2);
	bool m_bAutoColor;

public:
	Libxml();
	~Libxml();
	std::vector<Element*> parse(const char* filename);
	std::vector<Element *> parse(std::string file);
	bool isAutoColorActivated();
	long getTimeScale();
};

#endif