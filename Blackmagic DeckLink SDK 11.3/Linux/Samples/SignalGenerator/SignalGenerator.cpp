/* -LICENSE-START-
** Copyright (c) 2018 Blackmagic Design
**
** Permission is hereby granted, free of charge, to any person or organization
** obtaining a copy of the software and accompanying documentation covered by
** this license (the "Software") to use, reproduce, display, distribute,
** execute, and transmit the Software, and to prepare derivative works of the
** Software, and to permit third-parties to whom the Software is furnished to
** do so, all subject to the following:
** 
** The copyright notices in the Software and this entire statement, including
** the above license grant, this restriction and the following disclaimer,
** must be included in all copies of the Software, in whole or in part, and
** all derivative works of the Software, unless such copies or derivative
** works are solely in the form of machine-executable object code generated by
** a source language processor.
** 
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
** -LICENSE-END-
*/
//
//  SignalGenerator.cpp
//  Signal Generator
//

#include "SignalGenerator.h"
#include "DeckLinkOutputDevice.h"
#include "DeckLinkDeviceDiscovery.h"
#include "ProfileCallback.h"

#include <math.h>
#include <stdio.h>

const uint32_t		kAudioWaterlevel = 48000;

// SD 75% Colour Bars
static uint32_t gSD75pcColourBars[8] =
{
	0xeb80eb80, 0xa28ea22c, 0x832c839c, 0x703a7048,
	0x54c654b8, 0x41d44164, 0x237223d4, 0x10801080
};

// HD 75% Colour Bars
static uint32_t gHD75pcColourBars[8] =
{
	0xeb80eb80, 0xa888a82c, 0x912c9193, 0x8534853f,
	0x3fcc3fc1, 0x33d4336d, 0x1c781cd4, 0x10801080
};

// Audio channels supported
static const int gAudioChannels[] = { 2, 8, 16 };

// Supported pixel formats
static const QVector<QPair<BMDPixelFormat, QString>> kPixelFormats =
{
	qMakePair(bmdFormat8BitYUV,		QString("8-bit YUV")),
	qMakePair(bmdFormat10BitYUV,	QString("10-bit YUV")),
	qMakePair(bmdFormat8BitARGB,	QString("8-bit RGB")),
	qMakePair(bmdFormat10BitRGB,	QString("10-bit RGB")),
};

class CDeckLinkGLWidget : public QGLWidget, public IDeckLinkScreenPreviewCallback
{
private:
	QAtomicInt refCount;
	QMutex mutex;
	IDeckLinkOutput* deckLinkOutput;
	IDeckLinkGLScreenPreviewHelper* deckLinkScreenPreviewHelper;
	
public:
	CDeckLinkGLWidget(QWidget* parent);
	
	// IUnknown
	virtual HRESULT QueryInterface(REFIID iid, LPVOID *ppv);
	virtual ULONG AddRef();
	virtual ULONG Release();

	// IDeckLinkScreenPreviewCallback
	virtual HRESULT DrawFrame(IDeckLinkVideoFrame* theFrame);
	
protected:
	void initializeGL();
	void paintGL();
	void resizeGL(int width, int height);
};

CDeckLinkGLWidget::CDeckLinkGLWidget(QWidget* parent) : QGLWidget(parent)
{
	refCount = 1;
	
	deckLinkOutput = deckLinkOutput;
	deckLinkScreenPreviewHelper = CreateOpenGLScreenPreviewHelper();
}

void	CDeckLinkGLWidget::initializeGL ()
{
	if (deckLinkScreenPreviewHelper != NULL)
	{
		mutex.lock();
			deckLinkScreenPreviewHelper->InitializeGL();
		mutex.unlock();
	}
}

void	CDeckLinkGLWidget::paintGL ()
{
	mutex.lock();
		glLoadIdentity();

		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		deckLinkScreenPreviewHelper->PaintGL();
	mutex.unlock();
}

void	CDeckLinkGLWidget::resizeGL (int width, int height)
{
	mutex.lock();
		glViewport(0, 0, width, height);
	mutex.unlock();
}

HRESULT		CDeckLinkGLWidget::QueryInterface (REFIID, LPVOID *ppv)
{
	*ppv = NULL;
	return E_NOINTERFACE;
}

ULONG		CDeckLinkGLWidget::AddRef ()
{
	int		oldValue;
	
	oldValue = refCount.fetchAndAddAcquire(1);
	return (ULONG)(oldValue + 1);
}

ULONG		CDeckLinkGLWidget::Release ()
{
	int		oldValue;
	
	oldValue = refCount.fetchAndAddAcquire(-1);
	if (oldValue == 1)
	{
		delete this;
	}
	
	return (ULONG)(oldValue - 1);
}

HRESULT		CDeckLinkGLWidget::DrawFrame (IDeckLinkVideoFrame* theFrame)
{
	if (deckLinkScreenPreviewHelper != NULL)
	{
		deckLinkScreenPreviewHelper->SetFrame(theFrame);
		update();
	}
	return S_OK;
}

SignalGenerator::SignalGenerator()
	: QDialog()
{
	running = false;
	selectedDevice = NULL;
	deckLinkDiscovery = NULL;
	profileCallback = NULL;
	selectedDisplayMode = bmdModeUnknown;
	videoFrameBlack = NULL;
	videoFrameBars = NULL;
	audioBuffer = NULL;
	timeCode = NULL;
	scheduledPlaybackStopped = false;

	ui = new Ui::SignalGeneratorDialog();
	ui->setupUi(this);

	layout = new QGridLayout(ui->previewContainer);
	layout->setMargin(0);

	previewView = new CDeckLinkGLWidget(this);
	previewView->resize(ui->previewContainer->size());
	previewView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	layout->addWidget(previewView, 0, 0, 0, 0);
	previewView->DrawFrame(NULL);

	ui->outputSignalPopup->addItem("Pip", QVariant::fromValue((int)kOutputSignalPip));
	ui->outputSignalPopup->addItem("Dropout", QVariant::fromValue((int)kOutputSignalDrop));
	
	ui->audioSampleDepthPopup->addItem("16", QVariant::fromValue(16));
	ui->audioSampleDepthPopup->addItem("32", QVariant::fromValue(32));

	connect(ui->startButton, SIGNAL(clicked()), this, SLOT(toggleStart()));
	connect(ui->videoFormatPopup, SIGNAL(currentIndexChanged(int)), this, SLOT(videoFormatChanged(int)));
	connect(ui->outputDevicePopup, SIGNAL(currentIndexChanged(int)), this, SLOT(outputDeviceChanged(int)));
	enableInterface(false);
	show();
}

SignalGenerator::~SignalGenerator()
{
	if (selectedDevice != NULL)
	{
		if (selectedDevice->GetProfileManager() != NULL)
			selectedDevice->GetProfileManager()->SetCallback(NULL);

		selectedDevice->GetDeviceOutput()->SetScreenPreviewCallback(NULL);
	}

	if (previewView != NULL)
	{
		previewView->Release();
		previewView = NULL;
	}

	if (profileCallback != NULL)
	{
		profileCallback->Release();
		profileCallback = NULL;
	}

	for (int i = 0; i < ui->outputDevicePopup->count(); i++)
	{
		DeckLinkOutputDevice* deckLinkOutputDevice = (DeckLinkOutputDevice*)(((QVariant)ui->outputDevicePopup->itemData(i)).value<void*>());
		deckLinkOutputDevice->Release();
		deckLinkOutputDevice = NULL;
	}

	if (deckLinkDiscovery != NULL)
	{
		deckLinkDiscovery->Release();
		deckLinkDiscovery = NULL;
	}

	delete timeCode;
}

void SignalGenerator::setup()
{
	//
	// Create and initialise DeckLink device discovery and profile objects
	deckLinkDiscovery = new DeckLinkDeviceDiscovery(this);
	profileCallback = new ProfileCallback(this);
	if ((deckLinkDiscovery != NULL) && (profileCallback != NULL))
	{
		if (!deckLinkDiscovery->enable())
		{
			QMessageBox::critical(this, "This application requires the DeckLink drivers installed.", "Please install the Blackmagic DeckLink drivers to use the features of this application.");
		}
	}
}

void SignalGenerator::customEvent(QEvent *event)
{
	if (event->type() == ADD_DEVICE_EVENT)
	{
		SignalGeneratorEvent* sge = dynamic_cast<SignalGeneratorEvent*>(event);
		addDevice(sge->deckLink());
	}

	else if (event->type() == REMOVE_DEVICE_EVENT)
	{
		SignalGeneratorEvent* sge = dynamic_cast<SignalGeneratorEvent*>(event);
		removeDevice(sge->deckLink());
	}

	else if (event->type() == PROFILE_ACTIVATED_EVENT)
	{
		ProfileCallbackEvent* pce = dynamic_cast<ProfileCallbackEvent*>(event);
		updateProfile(pce->Profile());
	}
}

void SignalGenerator::closeEvent(QCloseEvent *)
{
	if (running)
		stopRunning();

	deckLinkDiscovery->disable();
}

void SignalGenerator::enableInterface(bool enable)
{
	// Set the enable state of user interface elements
	for (auto& combobox : ui->groupBox->findChildren<QComboBox*>())
	{
		combobox->setEnabled(enable);
	}
}

void SignalGenerator::toggleStart()
{
	if (running == false)
		startRunning();
	else
		stopRunning();
}

void SignalGenerator::refreshDisplayModeMenu(void)
{
	// Populate the display mode combo with a list of display modes supported by the installed DeckLink card
	IDeckLinkDisplayModeIterator*	displayModeIterator;
	IDeckLinkDisplayMode*			deckLinkDisplayMode;
	IDeckLinkOutput*                deckLinkOutput;

	// Populate the display mode menu with a list of display modes supported by the installed DeckLink card
	ui->videoFormatPopup->clear();

	deckLinkOutput = selectedDevice->GetDeviceOutput();
	
	if (deckLinkOutput->GetDisplayModeIterator(&displayModeIterator) != S_OK)
		return;

	while (displayModeIterator->Next(&deckLinkDisplayMode) == S_OK)
	{
		char*					modeName;
		bool					supported = false;
		BMDDisplayMode			mode = deckLinkDisplayMode->GetDisplayMode();

		if ((deckLinkOutput->DoesSupportVideoMode(bmdVideoConnectionUnspecified, mode, bmdFormatUnspecified, bmdSupportedVideoModeDefault, nullptr, &supported) == S_OK) && supported)
		{
			if (deckLinkDisplayMode->GetName(const_cast<const char**>(&modeName)) == S_OK)
			{
				ui->videoFormatPopup->addItem(QString(modeName), QVariant::fromValue((uint64_t)mode));
				free(modeName);
			}
		}

		deckLinkDisplayMode->Release();
		deckLinkDisplayMode = nullptr;
	}
	displayModeIterator->Release();

	ui->videoFormatPopup->setCurrentIndex(0);
	ui->startButton->setEnabled(ui->videoFormatPopup->count() != 0);
}

void SignalGenerator::refreshPixelFormatMenu(void)
{
	// Populate the pixel format mode combo with a list of pixel formats supported by the installed DeckLink card
	IDeckLinkOutput* deckLinkOutput = selectedDevice->GetDeviceOutput();

	ui->pixelFormatPopup->clear();

	for (auto& pixelFormat : kPixelFormats)
	{
		bool supported = false;
		HRESULT hr = deckLinkOutput->DoesSupportVideoMode(bmdVideoConnectionUnspecified, selectedDisplayMode, pixelFormat.first, bmdSupportedVideoModeDefault, NULL, &supported);
		if (hr != S_OK || ! supported)
			continue;

		ui->pixelFormatPopup->addItem(QString(pixelFormat.second), QVariant::fromValue((unsigned int)pixelFormat.first));
	}

	ui->pixelFormatPopup->setCurrentIndex(0);
}

void SignalGenerator::refreshAudioChannelMenu(void)
{
	IDeckLink*					deckLink;
	IDeckLinkProfileAttributes*	deckLinkAttributes = NULL;
	int64_t						maxAudioChannels;

	deckLink = selectedDevice->GetDeckLinkInstance();

	// Get DeckLink attributes to determine number of audio channels
	if (deckLink->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&deckLinkAttributes) != S_OK)
		goto bail;

	// Get max number of audio channels supported by DeckLink device
	if (deckLinkAttributes->GetInt(BMDDeckLinkMaximumAudioChannels, &maxAudioChannels) != S_OK)
		goto bail;

	ui->audioChannelPopup->clear();

	// Scan through Audio channel popup menu and disable invalid entries
	for (unsigned int i = 0; i < sizeof(gAudioChannels)/sizeof(*gAudioChannels); i++)
	{
		if (maxAudioChannels < (int64_t)gAudioChannels[i])
			break;
			
		QVariant audioChannelVariant = QVariant::fromValue(gAudioChannels[i]);

		ui->audioChannelPopup->addItem(audioChannelVariant.toString(), audioChannelVariant);
	}
	
	ui->audioChannelPopup->setCurrentIndex(ui->audioChannelPopup->count() - 1);

bail:
	if (deckLinkAttributes)
		deckLinkAttributes->Release();
}

void SignalGenerator::addDevice(IDeckLink* deckLink)
{
	DeckLinkOutputDevice* newDevice = new DeckLinkOutputDevice(this, deckLink);

	// Initialise new DeckLinkDevice object
	if (!newDevice->Init())
	{
		// Device does not have IDeckLinkOutput interface, eg it is a DeckLink Mini Recorder
		newDevice->Release();
		return;
	}

	// Add this DeckLink device to the device list
	ui->outputDevicePopup->addItem(newDevice->GetDeviceName(), QVariant::fromValue((void*)newDevice));

	if (ui->outputDevicePopup->count() == 1)
	{
		// We have added our first item, refresh and enable UI
		ui->outputDevicePopup->setCurrentIndex(0);
		outputDeviceChanged(0);

		enableInterface(true);
		ui->startButton->setText("Start");
	}
}

void SignalGenerator::removeDevice(IDeckLink* deckLink)
{
	int deviceIndex = -1; 
	DeckLinkOutputDevice* deviceToRemove = NULL;

	// Find the combo box entry to remove (there may be multiple entries with the same name, but each
	// will have a different data pointer).
	for (deviceIndex = 0; deviceIndex < ui->outputDevicePopup->count(); ++deviceIndex)
	{
		deviceToRemove = (DeckLinkOutputDevice*)(((QVariant)ui->outputDevicePopup->itemData(deviceIndex)).value<void*>());
		if (deviceToRemove->GetDeckLinkInstance() == deckLink)
			break;
	}

	if (deviceToRemove == NULL)
		return;

	// Remove device from list
	ui->outputDevicePopup->removeItem(deviceIndex);

	// If playback is ongoing, stop it
	if ( (selectedDevice == deviceToRemove) && running )
		stopRunning();

	// Check how many devices are left
	if (ui->outputDevicePopup->count() == 0)
	{
		// We have removed the last device, disable the interface.
		enableInterface(false);
		selectedDevice = NULL;
	}
	else if (selectedDevice == deviceToRemove)
	{
		// The device that was removed was the one selected in the UI.
		// Select the first available device in the list and reset the UI.
		ui->outputDevicePopup->setCurrentIndex(0);
		outputDeviceChanged(0);
	}

	// Release DeckLinkDevice instance
	deviceToRemove->Release();
}

void SignalGenerator::playbackStopped()
{
	// Notify waiting process that scheduled playback has stopped
	mutex.lock();
		scheduledPlaybackStopped = true;
	mutex.unlock();
	stopPlaybackCondition.wakeOne();
}

void SignalGenerator::haltStreams(void)
{
	// Profile is changing, stop playback if running
	if (running)
		stopRunning();
}

void SignalGenerator::updateProfile(IDeckLinkProfile* /* newProfile */)
{
	// Update the video mode popup menu based on new profile
	refreshDisplayModeMenu();

	// Update the audio channels popup menu based on new profile
	refreshAudioChannelMenu();
}

IDeckLinkMutableVideoFrame *SignalGenerator::CreateOutputFrame(std::function<void(IDeckLinkVideoFrame*)> fillFrame)
{
	IDeckLinkOutput*                deckLinkOutput;
	IDeckLinkMutableVideoFrame*		referenceFrame	= NULL;
	IDeckLinkMutableVideoFrame*		scheduleFrame	= NULL;
	HRESULT							hr;
	BMDPixelFormat					pixelFormat;
	int								bytesPerRow;
	int								referenceBytesPerRow;
	IDeckLinkVideoConversion*		frameConverter	= NULL;

	pixelFormat = (BMDPixelFormat)ui->pixelFormatPopup->itemData(ui->pixelFormatPopup->currentIndex()).value<int>();
	bytesPerRow = GetRowBytes(pixelFormat, frameWidth);
	referenceBytesPerRow = GetRowBytes(bmdFormat8BitYUV, frameWidth);

	deckLinkOutput = selectedDevice->GetDeviceOutput();

	frameConverter = CreateVideoConversionInstance();
	if (frameConverter == NULL)
		goto bail;

	hr = deckLinkOutput->CreateVideoFrame(frameWidth, frameHeight, referenceBytesPerRow, bmdFormat8BitYUV, bmdFrameFlagDefault, &referenceFrame);
	if (hr != S_OK)
		goto bail;

	fillFrame(referenceFrame);

	if (pixelFormat == bmdFormat8BitYUV)
	{
		// Frame is already 8-bit YUV, no conversion required
		scheduleFrame = referenceFrame;
		scheduleFrame->AddRef();
	}
	else
	{
		hr = deckLinkOutput->CreateVideoFrame(frameWidth, frameHeight, bytesPerRow, pixelFormat, bmdFrameFlagDefault, &scheduleFrame);
		if (hr != S_OK)
			goto bail;

		hr = frameConverter->ConvertFrame(referenceFrame, scheduleFrame);
		if (hr != S_OK)
			goto bail;
	}

bail:
	if (referenceFrame != NULL)
	{
		referenceFrame->Release();
		referenceFrame = NULL;
	}
	if (frameConverter != NULL)
	{
		frameConverter->Release();
		frameConverter = NULL;
	}

	return scheduleFrame;
}

void SignalGenerator::startRunning()
{
	IDeckLinkOutput*			deckLinkOutput		= selectedDevice->GetDeviceOutput();
	IDeckLinkDisplayMode*		displayMode			= nullptr;
	IDeckLinkProfileAttributes*	deckLinkAttributes	= nullptr;
	bool						success				= false;
	BMDVideoOutputFlags			videoOutputFlags	= 0;
	QVariant v;
	
	deckLinkOutput->SetScreenPreviewCallback(previewView);

	// Determine the audio and video properties for the output stream
	v = ui->outputSignalPopup->itemData(ui->outputSignalPopup->currentIndex());
	outputSignal = (OutputSignal)v.value<int>();
	
	v = ui->audioChannelPopup->itemData(ui->audioChannelPopup->currentIndex());
	audioChannelCount = v.value<int>();
	
	v = ui->audioSampleDepthPopup->itemData(ui->audioSampleDepthPopup->currentIndex());
	audioSampleDepth = v.value<int>();
	audioSampleRate = bmdAudioSampleRate48kHz;
	
	// Get the IDeckLinkDisplayMode object associated with the selected display mode
	if (deckLinkOutput->GetDisplayMode(selectedDisplayMode, &displayMode) != S_OK)
		goto bail;

	frameWidth = displayMode->GetWidth();
	frameHeight = displayMode->GetHeight();
	
	displayMode->GetFrameRate(&frameDuration, &frameTimescale);
	// Calculate the number of frames per second, rounded up to the nearest integer.  For example, for NTSC (29.97 FPS), framesPerSecond == 30.
	framesPerSecond = (frameTimescale + (frameDuration-1))  /  frameDuration;
	
	// m-rate frame rates with multiple 30-frame counting should implement Drop Frames compensation, refer to SMPTE 12-1
	if (frameDuration == 1001 && frameTimescale % 30000 == 0)
		dropFrames = 2 * (frameTimescale / 30000);
	else
		dropFrames = 0;

	displayMode->Release();

	// Check whether HFRTC is supported by the selected device
	if (selectedDevice->GetDeckLinkInstance()->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&deckLinkAttributes) != S_OK)
		goto bail;

	if (deckLinkAttributes->GetFlag(BMDDeckLinkSupportsHighFrameRateTimecode, &hfrtcSupported) != S_OK)
	{
		hfrtcSupported = false;
	}

	if (selectedDisplayMode == bmdModeNTSC ||
			selectedDisplayMode == bmdModeNTSC2398 ||
			selectedDisplayMode == bmdModePAL)
	{
		timeCodeFormat = bmdTimecodeVITC;
		videoOutputFlags |= bmdVideoOutputVITC;
	}
	else
	{
		timeCodeFormat = bmdTimecodeRP188Any;
		videoOutputFlags |= bmdVideoOutputRP188;
	}

	if (timeCode)
		delete timeCode;
	timeCode = new Timecode(framesPerSecond, dropFrames);


	// Set the video output mode
	if (deckLinkOutput->EnableVideoOutput(selectedDisplayMode, videoOutputFlags) != S_OK)
		goto bail;
	
	// Set the audio output mode
	if (deckLinkOutput->EnableAudioOutput(bmdAudioSampleRate48kHz, audioSampleDepth, audioChannelCount, bmdAudioOutputStreamTimestamped) != S_OK)
		goto bail;
	
	
	// Generate one second of audio tone
	audioSamplesPerFrame = ((audioSampleRate * frameDuration) / frameTimescale);
	audioBufferSampleLength = (framesPerSecond * audioSampleRate * frameDuration) / frameTimescale;
	audioBuffer = malloc(audioBufferSampleLength * audioChannelCount * (audioSampleDepth / 8));
	if (audioBuffer == NULL)
		goto bail;
	FillSine(audioBuffer, audioBufferSampleLength, audioChannelCount, audioSampleDepth);
	
	// Generate a frame of black
	videoFrameBlack = CreateOutputFrame(FillBlack);
	
	// Generate a frame of colour bars
	videoFrameBars = CreateOutputFrame(FillColourBars);
	
	// Begin video preroll by scheduling a second of frames in hardware
	totalFramesScheduled = 0;
	for (unsigned int i = 0; i < framesPerSecond; i++)
		scheduleNextFrame(true);
	
	// Begin audio preroll.  This will begin calling our audio callback, which will start the DeckLink output stream.
	totalAudioSecondsScheduled = 0;
	if (deckLinkOutput->BeginAudioPreroll() != S_OK)
		goto bail;
	
	// Success; update the UI
	running = true;
	ui->startButton->setText("Stop");
	// Disable the user interface while running (prevent the user from making changes to the output signal)
	enableInterface(false);
	scheduledPlaybackStopped = false;

	success = true;
	
bail:
	if (deckLinkAttributes != NULL)
	{
		deckLinkAttributes->Release();
	}

	if(!success)
	{
		QMessageBox::critical(this, "Failed to start output", "Failed to start output");
		// *** Error-handling code.  Cleanup any resources that were allocated. *** //
		stopRunning();
	}
}

void SignalGenerator::stopRunning()
{
	IDeckLinkOutput* deckLinkOutput = selectedDevice->GetDeviceOutput();

	if (running)
	{
		// Stop the audio and video output streams immediately
		deckLinkOutput->StopScheduledPlayback(0, NULL, 0);

		// Wait until IDeckLinkVideoOutputCallback::ScheduledPlaybackHasStopped callback
		mutex.lock();
			while (!scheduledPlaybackStopped)
				stopPlaybackCondition.wait(&mutex);
		mutex.unlock();
	}

	deckLinkOutput->SetScreenPreviewCallback(nullptr);

	deckLinkOutput->DisableAudioOutput();
	deckLinkOutput->DisableVideoOutput();
	
	if (videoFrameBlack != NULL)
		videoFrameBlack->Release();
	videoFrameBlack = NULL;
	
	if (videoFrameBars != NULL)
		videoFrameBars->Release();
	videoFrameBars = NULL;
	
	if (audioBuffer != NULL)
		free(audioBuffer);
	audioBuffer = NULL;
	
	// Success; update the UI
	running = false;
	ui->startButton->setText("Start");
	enableInterface(true);
}


void SignalGenerator::scheduleNextFrame(bool prerolling)
{
	HRESULT							result = S_OK;
	IDeckLinkMutableVideoFrame*		currentFrame;
	IDeckLinkOutput*				deckLinkOutput = nullptr;
	IDeckLinkDisplayMode*			outputDisplayMode = nullptr;
	bool							setVITC1Timecode = false;
	bool							setVITC2Timecode = false;

	deckLinkOutput = selectedDevice->GetDeviceOutput();

	if (prerolling == false)
	{
		// If not prerolling, make sure that playback is still active
		if (running == false)
			return;
	}
	
	if (outputSignal == kOutputSignalPip)
	{
		if ((totalFramesScheduled % framesPerSecond) == 0)
			currentFrame = videoFrameBars;
		else
			currentFrame = videoFrameBlack;
	}
	else
	{
		if ((totalFramesScheduled % framesPerSecond) == 0)
			currentFrame = videoFrameBlack;
		else
			currentFrame = videoFrameBars;
	}
	
	if (timeCodeFormat == bmdTimecodeVITC)
	{
		result = currentFrame->SetTimecodeFromComponents(bmdTimecodeVITC,
														 timeCode->hours(),
														 timeCode->minutes(),
														 timeCode->seconds(),
														 timeCode->frames(),
														 bmdTimecodeFlagDefault);
		if (result != S_OK)
		{
			fprintf(stderr, "Could not set VITC timecode on frame - result = %08x\n", result);
			goto bail;
		}
	}
	else
	{
		int frames = timeCode->frames();

		if (hfrtcSupported)
		{
			result = currentFrame->SetTimecodeFromComponents(bmdTimecodeRP188HighFrameRate,
														   timeCode->hours(),
														   timeCode->minutes(),
														   timeCode->seconds(),
														   frames,
														   bmdTimecodeFlagDefault);
			if (result != S_OK)
			{
				fprintf(stderr, "Could not set HFRTC timecode on frame - result = %08x\n", result);
				goto bail;
			}
		}

		result = deckLinkOutput->GetDisplayMode(selectedDisplayMode, &outputDisplayMode);
		if (result != S_OK)
		{
			fprintf(stderr, "Could not get output display mode - result = %08x\n", result);
			goto bail;			
		}

		if (outputDisplayMode->GetFieldDominance() != bmdProgressiveFrame)
		{
			// An interlaced or PsF frame has both VITC1 and VITC2 set with the same timecode value (SMPTE ST 12-2:2014 7.2)
			setVITC1Timecode = true;
			setVITC2Timecode = true;
		}
		else if (framesPerSecond <= 30)
		{
			// If this isn't a High-P mode, then just use VITC1 (SMPTE ST 12-2:2014 7.2)
			setVITC1Timecode = true;
		}
		else if (framesPerSecond <= 60)
		{
			// If this is a High-P mode then use VITC1 on even frames and VITC2 on odd frames. This is done because the
			// frames field of the RP188 VITC timecode cannot hold values greater than 30 (SMPTE ST 12-2:2014 7.2, 9.2)
			if ((frames & 1) == 0)
				setVITC1Timecode = true;
			else
				setVITC2Timecode = true;

			frames >>= 1;
		}

		if (setVITC1Timecode)
		{
			result = currentFrame->SetTimecodeFromComponents(bmdTimecodeRP188VITC1,
															 timeCode->hours(),
															 timeCode->minutes(),
															 timeCode->seconds(),
															 frames,
															 bmdTimecodeFlagDefault);
			if (result != S_OK)
			{
				fprintf(stderr, "Could not set VITC1 timecode on interlaced frame - result = %08x\n", result);
				goto bail;
			}
		}

		if (setVITC2Timecode)
		{
			// The VITC2 timecode also has the field mark flag set
			result = currentFrame->SetTimecodeFromComponents(bmdTimecodeRP188VITC2,
															 timeCode->hours(),
															 timeCode->minutes(),
															 timeCode->seconds(),
															 frames,
															 bmdTimecodeFieldMark);
			if (result != S_OK)
			{
				fprintf(stderr, "Could not set VITC1 timecode on interlaced frame - result = %08x\n", result);
				goto bail;
			}
		}
	}

	printf("Output frame: %02d:%02d:%02d:%03d\n", timeCode->hours(), timeCode->minutes(), timeCode->seconds(), timeCode->frames());

	if (deckLinkOutput->ScheduleVideoFrame(currentFrame, (totalFramesScheduled * frameDuration), frameDuration, frameTimescale) != S_OK)
		goto bail;
	
bail:
	totalFramesScheduled += 1;
	timeCode->update();

	if (outputDisplayMode != nullptr)
		outputDisplayMode->Release();
}

void SignalGenerator::writeNextAudioSamples()
{
	// Write one second of audio to the DeckLink API.
	
	if (outputSignal == kOutputSignalPip)
	{
		// Schedule one-frame of audio tone
		if (selectedDevice->GetDeviceOutput()->ScheduleAudioSamples(audioBuffer, audioSamplesPerFrame, (totalAudioSecondsScheduled * audioBufferSampleLength), audioSampleRate, NULL) != S_OK)
			return;
	}
	else
	{
		// Schedule one-second (minus one frame) of audio tone
		if (selectedDevice->GetDeviceOutput()->ScheduleAudioSamples(audioBuffer, (audioBufferSampleLength - audioSamplesPerFrame), (totalAudioSecondsScheduled * audioBufferSampleLength) + audioSamplesPerFrame, audioSampleRate, NULL) != S_OK)
			return;
	}
	
	totalAudioSecondsScheduled += 1;
}

void SignalGenerator::outputDeviceChanged(int selectedDeviceIndex)
{
	if (selectedDeviceIndex == -1)
		return;

	// Release profile callback from existing selected device
	if ((selectedDevice != NULL) && (selectedDevice->GetProfileManager() != NULL))
		selectedDevice->GetProfileManager()->SetCallback(NULL);

	QVariant selectedDeviceVariant = ui->outputDevicePopup->itemData(selectedDeviceIndex);
	
	selectedDevice = (DeckLinkOutputDevice*)(selectedDeviceVariant.value<void*>());

	// Register profile callback with newly selected device's profile manager
	if (selectedDevice->GetProfileManager() != NULL)
		selectedDevice->GetProfileManager()->SetCallback(profileCallback);

	// Update the video mode popup menu
	refreshDisplayModeMenu();
	
	// Update the audio channels popup menu
	refreshAudioChannelMenu();
}

void SignalGenerator::videoFormatChanged(int videoFormatIndex)
{
	if (videoFormatIndex == -1)
		return;

	selectedDisplayMode = (BMDDisplayMode)ui->videoFormatPopup->itemData(videoFormatIndex).value<uint64_t>();

	// Update pixel format popup menu
	refreshPixelFormatMenu();
}

/*****************************************/

int		GetRowBytes(BMDPixelFormat pixelFormat, uint32_t frameWidth)
{
	int bytesPerRow;

	// Refer to DeckLink SDK Manual - 2.7.4 Pixel Formats
	switch (pixelFormat)
	{
	case bmdFormat8BitYUV:
		bytesPerRow = frameWidth * 2;
		break;

	case bmdFormat10BitYUV:
		bytesPerRow = ((frameWidth + 47) / 48) * 128;
		break;

	case bmdFormat10BitRGB:
		bytesPerRow = ((frameWidth + 63) / 64) * 256;
		break;

	case bmdFormat8BitARGB:
	case bmdFormat8BitBGRA:
	default:
		bytesPerRow = frameWidth * 4;
		break;
	}

	return bytesPerRow;
}

void	FillSine (void* audioBuffer, uint32_t samplesToWrite, uint32_t channels, uint32_t sampleDepth)
{
	if (sampleDepth == 16)
	{
		int16_t*		nextBuffer;
		
		nextBuffer = (int16_t*)audioBuffer;
		for (uint32_t i = 0; i < samplesToWrite; i++)
		{
			int16_t		sample;
			
			sample = (int16_t)(24576.0 * sin((i * 2.0 * M_PI) / 48.0));
			for (uint32_t ch = 0; ch < channels; ch++)
				*(nextBuffer++) = sample;
		}
	}
	else if (sampleDepth == 32)
	{
		int32_t*		nextBuffer;
		
		nextBuffer = (int32_t*)audioBuffer;
		for (uint32_t i = 0; i < samplesToWrite; i++)
		{
			int32_t		sample;
			
			sample = (int32_t)(1610612736.0 * sin((i * 2.0 * M_PI) / 48.0));
			for (uint32_t ch = 0; ch < channels; ch++)
				*(nextBuffer++) = sample;
		}
	}
}

void	FillColourBars (IDeckLinkVideoFrame* theFrame)
{
	uint32_t*		nextWord;
	uint32_t		width;
	uint32_t		height;
	uint32_t*		bars;
	
	theFrame->GetBytes((void**)&nextWord);
	width = theFrame->GetWidth();
	height = theFrame->GetHeight();
	
	if (width > 720)
	{
		bars = gHD75pcColourBars;
	}
	else
	{
		bars = gSD75pcColourBars;
	}

	for (uint32_t y = 0; y < height; y++)
	{
		for (uint32_t x = 0; x < width; x+=2)
		{
			*(nextWord++) = bars[(x * 8) / width];
		}
	}
}

void	FillBlack (IDeckLinkVideoFrame* theFrame)
{
	uint32_t*		nextWord;
	uint32_t		width;
	uint32_t		height;
	uint32_t		wordsRemaining;
	
	theFrame->GetBytes((void**)&nextWord);
	width = theFrame->GetWidth();
	height = theFrame->GetHeight();
	
	wordsRemaining = (width*2 * height) / 4;
	
	while (wordsRemaining-- > 0)
		*(nextWord++) = 0x10801080;
}
