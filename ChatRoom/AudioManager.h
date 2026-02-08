#pragma once

#define _USE_MATH_DEFINES
#include <fmod.hpp>
#include <fmod_errors.h>
#include <cmath>
#include <thread>
#include <chrono>
#include <iostream>
#include <conio.h>
#include <vector>
#include <map>
#pragma comment(lib, "fmod_vc.lib")

class AudioManager {
private:
	FMOD::System* system;
	std::map<std::string, FMOD::Sound*> sounds;
public:
	AudioManager();
	~AudioManager();
	void init();
	void update();
	void playNotification(const std::string& name);
};