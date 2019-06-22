// qtractorTempoAdjustForm.cpp
//
/****************************************************************************
   Copyright (C) 2005-2019, rncbc aka Rui Nuno Capela. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*****************************************************************************/

#include "qtractorTempoAdjustForm.h"

#include "qtractorAbout.h"

#include "qtractorSession.h"

#include "qtractorAudioClip.h"

#include "qtractorMainForm.h"

#include <QMessageBox>
#include <QLineEdit>
#include <QTime>


#ifdef CONFIG_LIBAUBIO

#include "qtractorAudioEngine.h"

#include <QProgressBar>

#include <aubio/aubio.h>


// Audio clip beat-detection callback.
struct audioClipBeatDetectData
{	// Ctor.
	audioClipBeatDetectData(unsigned short iChannels,
		unsigned int iBlockSize, unsigned iSampleRate)
		: count(0), channels(iChannels), nstep(iBlockSize >> 2)
	{
		aubio = new_aubio_tempo("default", iBlockSize, nstep, iSampleRate);
		ibuf = new_fvec(nstep);
		obuf = new_fvec(1);
	}
	// Dtor.
	~audioClipBeatDetectData()
	{
		beats.clear();
		del_fvec(obuf);
		del_fvec(ibuf);
		del_aubio_tempo(aubio);
	}
	// Members.
	unsigned int count;
	unsigned short channels;
	unsigned int nstep;
	aubio_tempo_t *aubio;
	fvec_t *ibuf;
	fvec_t *obuf;
	QList<unsigned long> beats;
};


static void audioClipBeatDetect (
	float **ppFrames, unsigned int iFrames, void *pvArg )
{
	audioClipBeatDetectData *pData
		= static_cast<audioClipBeatDetectData *> (pvArg);

	unsigned int i = 0;

	while (i < iFrames) {

		unsigned int j = 0;

		for (; j < pData->nstep && i < iFrames; ++j, ++i) {
			float fSum = 0.0f;
			for (unsigned short n = 0; n < pData->channels; ++n)
				fSum += ppFrames[n][i];
			fvec_set_sample(pData->ibuf, fSum / float(pData->channels), j);
		}

		for (; j < pData->nstep; ++j)
			fvec_set_sample(pData->ibuf, 0.0f, j);

		aubio_tempo_do(pData->aubio, pData->ibuf, pData->obuf);

		const bool is_beat = bool(fvec_get_sample(pData->obuf, 0));
		if (is_beat)
			pData->beats.append(aubio_tempo_get_last(pData->aubio));
	}

	if (++(pData->count) > 100) {
		qtractorMainForm *pMainForm = qtractorMainForm::getInstance();
		if (pMainForm) {
			QProgressBar *pProgressBar = pMainForm->progressBar();
			pProgressBar->setValue(pProgressBar->value() + iFrames);
		}
		qtractorSession::stabilize();
		pData->count = 0;
	}
}

#endif	// CONFIG_LIBAUBIO


//----------------------------------------------------------------------------
// qtractorTempoAdjustForm -- UI wrapper form.

// Constructor.
qtractorTempoAdjustForm::qtractorTempoAdjustForm (
	QWidget *pParent, Qt::WindowFlags wflags )
	: QDialog(pParent, wflags)
{
	// Setup UI struct...
	m_ui.setupUi(this);

	// Initialize local time scale.
	m_pTimeScale = new qtractorTimeScale();
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession)
		m_pTimeScale->copy(*pSession->timeScale());

	m_pClip = NULL;
	m_pAudioClip = NULL;

	m_pTempoTap = new QTime();
	m_iTempoTap = 0;
	m_fTempoTap = 0.0f;

	m_ui.RangeStartSpinBox->setTimeScale(m_pTimeScale);
	m_ui.RangeLengthSpinBox->setTimeScale(m_pTimeScale);

	m_ui.TempoSpinBox->setTempo(m_pTimeScale->tempo(), false);
	m_ui.TempoSpinBox->setBeatsPerBar(m_pTimeScale->beatsPerBar(), false);
	m_ui.TempoSpinBox->setBeatDivisor(m_pTimeScale->beatDivisor(), true);

	// Set proper time scales display format...
	m_ui.FormatComboBox->setCurrentIndex(int(m_pTimeScale->displayFormat()));

	// Initialize dirty control state (nope).
	m_iDirtySetup = 0;
	m_iDirtyCount = 0;

	// Try to set minimal window positioning.
	adjustSize();

	// UI signal/slot connections...
	QObject::connect(m_ui.TempoSpinBox,
		SIGNAL(valueChanged(float, unsigned short, unsigned short)),
		SLOT(tempoChanged()));
	QObject::connect(m_ui.TempoPushButton,
		SIGNAL(clicked()),
		SLOT(tempoTap()));

	QObject::connect(m_ui.RangeStartSpinBox,
		SIGNAL(valueChanged(unsigned long)),
		SLOT(rangeStartChanged(unsigned long)));
	QObject::connect(m_ui.RangeStartSpinBox,
		SIGNAL(displayFormatChanged(int)),
		SLOT(formatChanged(int)));
	QObject::connect(m_ui.RangeLengthSpinBox,
		SIGNAL(valueChanged(unsigned long)),
		SLOT(rangeLengthChanged(unsigned long)));
	QObject::connect(m_ui.RangeLengthSpinBox,
		SIGNAL(displayFormatChanged(int)),
		SLOT(formatChanged(int)));
	QObject::connect(m_ui.RangeBeatsSpinBox,
		SIGNAL(valueChanged(int)),
		SLOT(rangeBeatsChanged(int)));
	QObject::connect(m_ui.FormatComboBox,
		SIGNAL(activated(int)),
		SLOT(formatChanged(int)));
	QObject::connect(m_ui.AdjustPushButton,
		SIGNAL(clicked()),
		SLOT(adjust()));

	QObject::connect(m_ui.DialogButtonBox,
		SIGNAL(accepted()),
		SLOT(accept()));
	QObject::connect(m_ui.DialogButtonBox,
		SIGNAL(rejected()),
		SLOT(reject()));
}


// Destructor.
qtractorTempoAdjustForm::~qtractorTempoAdjustForm (void)
{
	// Don't forget to get rid of local time-scale instance...
	if (m_pTimeScale)
		delete m_pTimeScale;
	if (m_pTempoTap)
		delete m_pTempoTap;
}


// Clip accessors.
void qtractorTempoAdjustForm::setClip ( qtractorClip *pClip )
{
	m_pClip = pClip;

	if (m_pClip) {
		const unsigned long	iClipStart  = m_pClip->clipStart();
		const unsigned long	iClipLength = m_pClip->clipLength();
		m_ui.RangeStartSpinBox->setMinimum(iClipStart);
		m_ui.RangeStartSpinBox->setMaximum(iClipStart + iClipLength);
		m_ui.RangeLengthSpinBox->setMaximum(iClipLength);
	}

	if (m_pClip && m_pClip->track() &&
		m_pClip->track()->trackType() == qtractorTrack::Audio)
		m_pAudioClip = static_cast<qtractorAudioClip *> (m_pClip);
	else
		m_pAudioClip = NULL;
}

qtractorClip *qtractorTempoAdjustForm::clip (void) const
{
	return m_pClip;
}

qtractorAudioClip *qtractorTempoAdjustForm::audioClip (void) const
{
	return m_pAudioClip;
}


// Range accessors.
void qtractorTempoAdjustForm::setRangeStart ( unsigned long iRangeStart )
{
	++m_iDirtySetup;
	m_ui.RangeStartSpinBox->setValue(iRangeStart, true);
	m_ui.RangeLengthSpinBox->setDeltaValue(true, iRangeStart);
	--m_iDirtySetup;
}

unsigned long qtractorTempoAdjustForm::rangeStart (void) const
{
	return m_ui.RangeStartSpinBox->value();
}

void qtractorTempoAdjustForm::setRangeLength ( unsigned long iRangeLength )
{
	++m_iDirtySetup;
	m_ui.RangeLengthSpinBox->setValue(iRangeLength, true);
	updateRangeLength(iRangeLength);
	--m_iDirtySetup;
}

unsigned long qtractorTempoAdjustForm::rangeLength (void) const
{
	return m_ui.RangeLengthSpinBox->value();
}


void qtractorTempoAdjustForm::setRangeBeats ( unsigned short iRangeBeats )
{
	++m_iDirtySetup;
	m_ui.RangeBeatsSpinBox->setValue(iRangeBeats);
	--m_iDirtySetup;
}

unsigned short qtractorTempoAdjustForm::rangeBeats (void) const
{
	return m_ui.RangeBeatsSpinBox->value();
}


// Accepted results accessors.
float qtractorTempoAdjustForm::tempo (void) const
{
	return m_ui.TempoSpinBox->tempo();
}

unsigned short qtractorTempoAdjustForm::beatsPerBar (void) const
{
	return m_ui.TempoSpinBox->beatsPerBar();
}

unsigned short qtractorTempoAdjustForm::beatDivisor (void) const
{
	return m_ui.TempoSpinBox->beatDivisor();
}


// Tempo signature has changed.
void qtractorTempoAdjustForm::tempoChanged (void)
{
	if (m_iDirtySetup > 0)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorTempoAdjustForm::tempoChanged()");
#endif

	m_iTempoTap = 0;
	m_fTempoTap = 0.0f;

	const float fTempo = m_ui.TempoSpinBox->tempo();
	if (fTempo > 0.0f) {
		const unsigned long iBeatLength
			= 60.0f * float(m_pTimeScale->sampleRate()) / fTempo;
		if (iBeatLength > 0) {
			const unsigned long iRangeLength
				= m_ui.RangeLengthSpinBox->value();
			setRangeBeats(iRangeLength / iBeatLength);
		}
	}

	changed();
}


// Tempo tap click.
void qtractorTempoAdjustForm::tempoTap (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorTempoAdjustForm::tempoTap()");
#endif

	const int iTimeTap = m_pTempoTap->restart();
	if (iTimeTap > 200 && iTimeTap < 2000) { // Magic!
		m_fTempoTap += (60000.0f / float(iTimeTap));
		if (++m_iTempoTap > 2) {
			m_fTempoTap /= float(m_iTempoTap);
			m_iTempoTap  = 1; // Median-like averaging...
			m_ui.TempoSpinBox->setTempo(int(m_fTempoTap), true);
		}
	} else {
		m_iTempoTap = 0;
		m_fTempoTap = 0.0f;
	}
}


// Adjust delta-value spin-boxes to new anchor frame.
void qtractorTempoAdjustForm::rangeStartChanged ( unsigned long iRangeStart )
{
	if (m_iDirtySetup > 0)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorTempoAdjustForm::rangeStartChanged(%lu)", iRangeStart);
#endif

	m_ui.RangeLengthSpinBox->setDeltaValue(true, iRangeStart);

	updateRangeSelect();
	changed();
}


void qtractorTempoAdjustForm::rangeLengthChanged ( unsigned long iRangeLength )
{
	if (m_iDirtySetup > 0)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorTempoAdjustForm::rangeLengthChanged(%lu)", iRangeLength);
#endif

	updateRangeLength(iRangeLength);

	updateRangeSelect();
	changed();
}


void qtractorTempoAdjustForm::rangeBeatsChanged ( int iRangeBeats )
{
	if (m_iDirtySetup > 0)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorTempoAdjustForm::rangeBeatsChanged(%d)", iRangeBeats);
#endif

	changed();
}


// Display format has changed.
void qtractorTempoAdjustForm::formatChanged ( int iDisplayFormat )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorTempoAdjustForm::formatChanged()");
#endif

	const bool bBlockSignals = m_ui.FormatComboBox->blockSignals(true);
	m_ui.FormatComboBox->setCurrentIndex(iDisplayFormat);

	qtractorTimeScale::DisplayFormat displayFormat
		= qtractorTimeScale::DisplayFormat(iDisplayFormat);

	m_ui.RangeStartSpinBox->setDisplayFormat(displayFormat);
	m_ui.RangeLengthSpinBox->setDisplayFormat(displayFormat);

	if (m_pTimeScale)
		m_pTimeScale->setDisplayFormat(displayFormat);

	m_ui.FormatComboBox->blockSignals(bBlockSignals);

	stabilizeForm();
}


// Adjust as instructed.
void qtractorTempoAdjustForm::adjust (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorTempoAdjustForm::adjust()");
#endif

	const unsigned short iRangeBeats = m_ui.RangeBeatsSpinBox->value();
	if (iRangeBeats < 1)
		return;

	const unsigned long iRangeLength = m_ui.RangeLengthSpinBox->value();
	const unsigned long iBeatLength = iRangeLength / iRangeBeats;

	const float fTempo
		= 60.0f * float(m_pTimeScale->sampleRate()) / float(iBeatLength);
	m_ui.TempoSpinBox->setTempo(fTempo, false);

//	m_ui.RangeLengthSpinBox->setValue(iRangeBeats * iBeatLength, false);
	updateRangeSelect();
	changed();
}


// Audio clip beat-detector method .
void qtractorTempoAdjustForm::detect (void)
{
	if (m_pAudioClip == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorTempoAdjustForm::detect()");
#endif

#ifdef CONFIG_LIBAUBIO

	qtractorTrack *pTrack = m_pAudioClip->track();
	if (pTrack == NULL)
		return;

	qtractorSession *pSession = pTrack->session();
	if (pSession == NULL)
		return;

	qtractorAudioBus *pAudioBus
	= static_cast<qtractorAudioBus *> (pTrack->outputBus());
	if (pAudioBus == NULL)
		return;

	const unsigned short iChannels = pAudioBus->channels();
	const unsigned int iSampleRate = pSession->sampleRate();

	const unsigned long iRangeStart  = m_ui.RangeStartSpinBox->value();
	const unsigned long iRangeLength = m_ui.RangeLengthSpinBox->value();

	const unsigned long iOffset = iRangeStart - m_pAudioClip->clipStart();;
	const unsigned long iLength = iRangeLength;

	QProgressBar *pProgressBar = NULL;
	qtractorMainForm *pMainForm = qtractorMainForm::getInstance();
	if (pMainForm)
		pProgressBar = pMainForm->progressBar();
	if (pProgressBar) {
		pProgressBar->setRange(0, iLength / 100);
		pProgressBar->reset();
		pProgressBar->show();
	}
	audioClipBeatDetectData data(iChannels, 1024, iSampleRate);
	m_pAudioClip->clipExport(audioClipBeatDetect, &data, iOffset, iLength);
	if (pProgressBar)
		pProgressBar->hide();

	if (!data.beats.isEmpty()) {
		const float fTempo
			= aubio_tempo_get_bpm(data.aubio);
		m_ui.TempoSpinBox->setTempo(fTempo, true);
	}

#endif	// CONFIG_LIBAUBIO
}


// Dirty up settings.
void qtractorTempoAdjustForm::changed (void)
{
	if (m_iDirtySetup > 0)
		return;

	++m_iDirtyCount;
	stabilizeForm();
}


// Accept settings (OK button slot).
void qtractorTempoAdjustForm::accept (void)
{
	// Just go with dialog acceptance.
	QDialog::accept();
}


// Reject settings (Cancel button slot).
void qtractorTempoAdjustForm::reject (void)
{
	bool bReject = true;

	// Check if there's any pending changes...
	if (m_iDirtyCount > 0) {
		QMessageBox::StandardButtons buttons
			= QMessageBox::Discard | QMessageBox::Cancel;
		if (m_ui.DialogButtonBox->button(QDialogButtonBox::Ok)->isEnabled())
			buttons |= QMessageBox::Apply;
		switch (QMessageBox::warning(this,
			tr("Warning") + " - " QTRACTOR_TITLE,
			tr("Some settings have been changed.\n\n"
			"Do you want to apply the changes?"),
			buttons)) {
		case QMessageBox::Apply:
			accept();
			return;
		case QMessageBox::Discard:
			break;
		default:    // Cancel.
			bReject = false;
		}
	}

	if (bReject)
		QDialog::reject();
}


// Adjust current range beat count from length.
void qtractorTempoAdjustForm::updateRangeLength ( unsigned long iRangeLength )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorTempoAdjustForm::updateRangeLength(%lu)", iRangeLength);
#endif

	const int iRangeBeatsMax // It follows from max. tempo = 300bpm.
		= int(5.0f * float(iRangeLength) / float(m_pTimeScale->sampleRate()));
	m_ui.RangeBeatsSpinBox->setMaximum(iRangeBeatsMax);

	const unsigned long q = m_pTimeScale->beatsPerBar();
	const unsigned int iRangeBeats = m_pTimeScale->beatFromFrame(iRangeLength);
	setRangeBeats(q * ((iRangeBeats + (q >> 1)) / q));
}


// Adjust current selection edit/tail.
void qtractorTempoAdjustForm::updateRangeSelect (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorTempoAdjustForm::updateRangeSelect()");
#endif

	const unsigned long iRangeStart  = m_ui.RangeStartSpinBox->value();
	const unsigned long iRangeLength = m_ui.RangeLengthSpinBox->value();

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession) {
		pSession->setEditHead(iRangeStart);
		pSession->setEditTail(iRangeStart + iRangeLength);
	}

	qtractorMainForm *pMainForm = qtractorMainForm::getInstance();
	if (pMainForm)
		pMainForm->selectionNotifySlot(NULL);
}


// Stabilize current form state.
void qtractorTempoAdjustForm::stabilizeForm (void)
{
	const unsigned long iRangeLength = m_ui.RangeLengthSpinBox->value();
	const unsigned short iRangeBeats = m_ui.RangeBeatsSpinBox->value();

	bool bValid = (m_iDirtyCount > 0);
	bValid = bValid && (iRangeLength > 0);
	bValid = bValid && (iRangeBeats > 0);
	m_ui.AdjustPushButton->setEnabled(bValid);
//	m_ui.OkPushButton->setEnabled(bValid);
}


// end of qtractorTempoAdjustForm.cpp
