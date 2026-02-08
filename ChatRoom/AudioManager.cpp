#include "AudioManager.h"

AudioManager::AudioManager() : system(nullptr) {}
AudioManager::~AudioManager() {
	for (auto& [name, sound] : sounds) {
		if (sound) sound->release();
	}
	if (system) {
		system->close();
		system->release();
	}
}
void AudioManager::init() {
	FMOD::System_Create(&system);
	system->init(512, FMOD_INIT_NORMAL, NULL);
	FMOD::Sound* tempSound = nullptr;
	system->createSound("AudioClips/DM.mp3", FMOD_DEFAULT, nullptr, &tempSound);
	sounds["DM"] = tempSound;
	system->createSound("AudioClips/Global.mp3", FMOD_DEFAULT, nullptr, &tempSound);
	sounds["Global"] = tempSound;
	system->createSound("AudioClips/Server.mp3", FMOD_DEFAULT, nullptr, &tempSound);
	sounds["Server"] = tempSound;
}
void AudioManager::update() {
	if (system)
		system->update();
}
void AudioManager::playNotification(const std::string& name) {
	if(system && sounds.count(name) && sounds[name])
		system->playSound(sounds[name], NULL, false, nullptr);
}