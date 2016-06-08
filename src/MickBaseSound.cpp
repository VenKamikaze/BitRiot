/*
 * MickBaseSound.cpp
 *
 *  Created on: Mar 10, 2012
 *      Author: msaun
 */

#include "MickBaseSound.h"

static MickBaseSound *s_base_instance = 0;

MickBaseSound::MickBaseSound()
{
	initialised = false;
}

void MickBaseSound::setBaseInstance(MickBaseSound* base_inst)
{
	s_base_instance = base_inst;
}

MickBaseSound* MickBaseSound::getInstance()
{
	return s_base_instance;
}

MickBaseSound::~MickBaseSound() {
	// TODO Auto-generated destructor stub
}
