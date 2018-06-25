/*! \file recovermainwindow.h
 * \brief App main header
 * \copyright Christophe Seyve \em cseyve@free.fr
 *
 * Main header for the recovery program
 */
/*
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "recovermainwindow.h"
#include "ui_recovermainwindow.h"

#include <QSettings>
#include <QImage>
#include <QPixmap>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <QTimer>
#include <QMessageBox>

te_log_level g_log_level = LOG_INFO;

/******************************************************************************
 *
 * EXTRACTOR CODE
 *
 ******************************************************************************/
RecoverExtractor::RecoverExtractor()
	: QObject() {
	mBufferRaw = NULL;
	init();
}

RecoverExtractor::~RecoverExtractor() {
	purge();
}

void RecoverExtractor::init() {
	// Clear all data to reset to new open file
	mLastPosition = 0;
	mImageIndex = 0;
	mImageSize = 0;
	mStatus = tr("Init");
	mBufferMaxLen = MAX_JPEG_LEN;
	mProgress = 0;

	memset(mTag, 0, sizeof(uint8_t) * 5);
	mTag32 = 0;
}

void RecoverExtractor::purge() {
	mProgress = 100;

	CPP_DELETE_ARRAY(mBufferRaw);
	if(mFile.isOpen()) {
		mFile.close();
	}
}

void RecoverExtractor::setFilename(const QString & filename) {
	purge();
	init();

	mFilename = filename;
	QFileInfo fi(mFilename);
	mDir = fi.absoluteDir();
}

/******************************************************************************
 *
 * EXTRACTOR MAIN WINDOW UI
 *
 ******************************************************************************/
RecoverMainWindow::RecoverMainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::RecoverMainWindow)
{
	loadSettings();

	ui->setupUi(this);
}

RecoverMainWindow::~RecoverMainWindow()
{
	saveSettings();
	delete ui;
}

void RecoverMainWindow::loadSettings() {
	QSettings settings("RecoverMov");
	if(settings.value("LastDir").isValid()) {
		mLastDir = settings.value("LastDir").toString();
	}
}
void RecoverMainWindow::saveSettings() {
	QSettings settings("RecoverMov");
	if(!mLastDir.isEmpty()) {
		settings.setValue("LastDir", mLastDir);
	}
}

void RecoverMainWindow::on_openButton_clicked()
{
	QString filename = QFileDialog::getOpenFileName(this, tr("Open broken MJPEG file"), mLastDir,
													tr("MJPEG Movies (*.mov *.MOV *.avi *.AVI);;All files (*)"));
	if(filename.isEmpty()) { return; }

	QFileInfo fi(filename);
	mLastDir = fi.absoluteDir().absolutePath();
	mRecoverExtractor.setFilename(filename);

	on_stepButton_clicked();
}

void RecoverMainWindow::on_stepButton_clicked()
{
	bool ok = mRecoverExtractor.extract();
	if(!ok) {
		QMessageBox::warning(this, tr("Read image failed"), mRecoverExtractor.getStatus());
	} else {
		QPixmap pixmap = QPixmap::fromImage(mRecoverExtractor.getImage().scaled(ui->imageLabel->width(),
																				ui->imageLabel->height(),
																				Qt::KeepAspectRatio));

		ui->imageLabel->setPixmap(pixmap);
		if(ui->goOnCheckBox->isChecked()) {
			QTimer::singleShot(100, this, SLOT(on_stepButton_clicked()));
		}
	}

	QString status = mRecoverExtractor.getStatus();
	ui->debugLabel->setText(status);
	ui->progressBar->setValue(mRecoverExtractor.getProgress());
}

bool RecoverExtractor::extract() {
	if(mFilename.isEmpty()) {
		mStatus = tr("No file selected");
		return false;
	}

	// Try load a buffer and read from the buffer
	if(!mBufferRaw) {
		CPP_ALLOC_ARRAY(mBufferRaw, unsigned char, mBufferMaxLen);
		mFile.setFileName(mFilename);
		if(mFile.open(QFile::ReadOnly)) {
			QFileInfo fi(mFilename);
			mDir = fi.absoluteDir();

			if(mFile.size() == 0) {
				mStatus = tr("Empty file ") + mFilename;
				return false;
			}
		} else {
			CPP_DELETE_ARRAY(mBufferRaw);
		}
	}

	if(mBufferRaw) {
		bool ok = mFile.seek(mLastPosition);
		int readBytes = mFile.read((char *)mBufferRaw, mBufferMaxLen);
		mProgress = (int)(0.5 + 100. * mFile.pos() / mFile.size());
		if(!ok || readBytes <= 0) {
			mStatus = tr("Read failed for pos=")
					+ QString::number(mLastPosition)
					+ tr(" mBufferMaxLen=")
					+ QString::number(mBufferMaxLen)
					+ tr("read=") + QString::number(readBytes)
					;
			if(mFile.atEnd()) {
				mProgress = 100;
				mStatus = tr("End of file failed for pos=")
						+ QString::number(mLastPosition)
						+ tr(" mBufferMaxLen=")
						+ QString::number(mBufferMaxLen)
						;
				return true;
			}
			return false;
		}

		// Decode image
		QImage loadImage;

		MSG_PRINT(LOG_DEBUG, "Starting at mLastPosition=%d Index=%d "
							 "MinStepSize=%d read=%d tag='%s'",
				  mLastPosition,
				  mImageIndex, mImageSize,
				  readBytes,
				  mTag);

		// We try to skip the start of the file to avoid the magic number
		// so the jpeg buffer might be found
		bool foundit = false;
		int found_at = 0;
		if(mImageIndex < 2 || mTag32==0 || mBufferMaxLen <= 0) { // we don't know the tag, we check all positions
			int offset = 0;
			for(; !foundit && offset < readBytes-1000; offset++) {
				foundit = loadImage.loadFromData(mBufferRaw+offset, readBytes-offset, "JPG");
				if(foundit) {
					found_at = offset;
				}
			}

			// At first image, we check the header
			if(mImageIndex == 0) {
				mTag[0] = mBufferRaw[found_at + 0];
				mTag[1] = mBufferRaw[found_at + 1];
				mTag[2] = mBufferRaw[found_at + 2];
				mTag[3] = mBufferRaw[found_at + 3];
				mTag32 = *(uint32_t *)(mBufferRaw + found_at);
			}
			// At second, we check if it's the same so we can accelerate the search
			else if(mImageIndex >= 1) {
				uint8_t tag[4];
				tag[0] = mBufferRaw[found_at + 0];
				tag[1] = mBufferRaw[found_at + 1];
				tag[2] = mBufferRaw[found_at + 2];
				tag[3] = mBufferRaw[found_at + 3];
				uint32_t tag32 = *(uint32_t *)(mBufferRaw + found_at);

				MSG_PRINT(LOG_DEBUG, "Current header: 1st=0x%04x='%s' =? cur=0x%04x='%s'",
						  mTag32, mTag, tag32, tag
						  );

				if(tag32 != mTag32) {
					MSG_PRINT(LOG_ERROR, "Not constant header: 1st=0x%04x != 2nd=0x%04x",
							  mTag32, tag32
							  );
					mTag32 = 0; // So the search won't be accelerated
				}
			}


		} else {
			int offset = 0;
			MSG_PRINT(LOG_DEBUG, "Using accelerated from %d, read=%d", mLastPosition, readBytes);
			for( ; !foundit && offset < readBytes-1000; offset++) {
				//fprintf(stdout, "Searching at offset=%d memcompared=%d tag='%s'\n",
				//		offset, memcmp(tag, buffer+offset, 4), tag); fflush(stdout);
				uint32_t buffer32 = *(uint32_t *)(mBufferRaw+offset);
				if(mTag32 == buffer32) {
					if(offset > 0)
					{
						for(int dec = 0; !foundit && dec <16; dec++) {
							if(offset >= dec) {
								foundit = loadImage.loadFromData(mBufferRaw+offset-dec, readBytes-(offset-dec), "JPG");
								if(foundit) {
									MSG_PRINT(LOG_DEBUG, "Decoded for dec=%d", dec);
									found_at = offset-dec;
								}
							}
						}
					}
					if(!foundit) {
						MSG_PRINT(LOG_WARNING, "at %d, tag=0x%04x but not readable jpeg in (%p, %d)",
								  mLastPosition+offset, buffer32, mBufferRaw+offset, readBytes-offset
								  );
					}
				}
			}
			// if not found, try the not accelerated version
			if(!foundit) {
				MSG_PRINT(LOG_WARNING, "Cannot find JPEG with accelerated tag=0x%04x, revert to normal", mTag32);
				mTag32 = 0;
				offset = 0;
				for(; !foundit && offset < readBytes-1000; offset++) {
					foundit = loadImage.loadFromData(mBufferRaw+offset, readBytes-offset, "JPG");
				}
				if(foundit) {
					MSG_PRINT(LOG_INFO, "Failback to normal=> found at %d", mLastPosition+offset);
				}
			}
		}
		if(!foundit) {
			if(mFile.atEnd()) {
				mStatus = tr("End of file, finished");
				return true;
			}

			mStatus = tr("No jpeg found here");
			return false;
		} else {
			MSG_PRINT(LOG_TRACE, "   => Found it at %d", mLastPosition + found_at);

			// JPEG was found a
			mLastPosition += found_at;

			if(mImageIndex == 1) {
				// once we are on the second image, wa know the size of the first image, so
				// so we can adapt the step to not read
				mImageSize = mLastPosition * 0.75f; // 3/4 of image size is the new increment
			//	mBufferMaxLen = std::min(MAX_JPEG_LEN, (int)(mLastPosition * 2.5f));
			}


			// Save image
			mImageIndex++;
			MSG_PRINT(LOG_DEBUG, "    => Found JPG #%d at offset=%d",
					  mImageIndex,
					  mLastPosition);

			MSG_PRINT(LOG_DEBUG, "    => Found JPG #%d at offset=%d buffer=%x%x%x%x JFIF='%c%c%c%c' tag='%c%c%c%c'",
					  mImageIndex,
					  mLastPosition,
					  mBufferRaw[found_at+0],
					  mBufferRaw[found_at+1],
					  mBufferRaw[found_at+2],
					  mBufferRaw[found_at+3],
					  mBufferRaw[found_at+0]<' '?'x' : *(char *)&mBufferRaw[found_at+0],
					  mBufferRaw[found_at+1]<' '?'x' : *(char *)&mBufferRaw[found_at+1],
					  mBufferRaw[found_at+2]<' '?'x' : *(char *)&mBufferRaw[found_at+2],
					  mBufferRaw[found_at+3]<' '?'x' : *(char *)&mBufferRaw[found_at+3],
					  mTag[0]<' '?'x' : *(char *)&mTag[0],
					  mTag[1]<' '?'x' : *(char *)&mTag[1],
					  mTag[2]<' '?'x' : *(char *)&mTag[2],
					  mTag[3]<' '?'x' : *(char *)&mTag[3]
					  );

			QString str;
			str.sprintf("Found JPG #%d at %.1f MB",
						mImageIndex,
						(float) mLastPosition / (1024.f*1024.f));

			// add an offset so we don't read again the same image
			if(mImageIndex <= 2) {
				mLastPosition += 10000; // min image size 200 kB for 2K video
			} else {
				mLastPosition += mImageSize;
			}
			mStatus = str;

			mLoadImage = loadImage.copy();


			QString recoveredImageName;
			recoveredImageName.sprintf("REC_%04d.jpg", mImageIndex);
			MSG_PRINT(LOG_DEBUG, "Saving '%s'", qPrintable(recoveredImageName));

			QString imageFile = mDir.absoluteFilePath(recoveredImageName);
			ok = loadImage.save(imageFile, "JPG", 92);
			if(!ok) {
				mStatus = tr("Cannot save image ") + imageFile;
			}
			return ok;
		}
	}

	//CPP_DELETE_ARRAY(buffer);
	return false;
}





void registerAlloc(const char * file, const char *func, int line,
				   void * buf, size_t size) {
	fprintf(stderr, "%s:%s:%d: allocate %p / %u bytes", file, func, line, buf, size);
}

void registerDelete(const char * file, const char *func, int line,
					void * buf) {
	fprintf(stderr, "%s:%s:%d: delete %p", file, func, line, buf);
}

