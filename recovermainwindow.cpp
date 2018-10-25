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

#include <assert.h>

/// \brief Global log level for this file
te_log_level g_log_level = LOG_INFO;



const char * c_log_descr[] = {
	"CRITICAL",
	"ERROR",
	"WARNING",
	"INFO",
	"DEBUG",
	"TRACE"
};

const char * log_descr(int lvl) {
	assert(lvl < 6);
	return c_log_descr[lvl];
}

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
	mBufferRaw = NULL;
	mDoubleJpegBuffer[0] = NULL;
	mDoubleJpegBuffer[1] = NULL;
	mDoubleJpegBufferSize[0] = 0;
	mDoubleJpegBufferSize[1] = 0;
	mDoubleJpegBufferIndex = 0;

	mProgress = 0;

	memset(mTag, 0, sizeof(uint8_t) * 5);
	mTag32 = 0;
}

void RecoverExtractor::purge() {
	mProgress = 100;

	CPP_DELETE_ARRAY(mBufferRaw);
	mBufferMaxLen = 0;

	CPP_DELETE_ARRAY(mDoubleJpegBuffer[0]);
	CPP_DELETE_ARRAY(mDoubleJpegBuffer[1]);
	mDoubleJpegBufferSize[0] = 0;
	mDoubleJpegBufferSize[1] = 0;

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

    // Create the subdir
    mDir.mkdir(fi.baseName());
    mDir.cd(fi.baseName());

    QString ExportDir = mDir.absolutePath();
    MSG_PRINT(LOG_INFO, "Saving images in '%s'", qPrintable(ExportDir));
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
	ui->toolbarWidget->setEnabled(false);
	bool ok = mRecoverExtractor.extract();
	if(!ok) {
		QMessageBox::warning(this, tr("Read image failed"), mRecoverExtractor.getStatus());
    } else {
        QPixmap pixmap = QPixmap::fromImage(mRecoverExtractor.getImage().scaled(ui->imageLabel->width(),
                                                                                ui->imageLabel->height(),
                                                                                Qt::KeepAspectRatio));

        ui->imageLabel->setPixmap(pixmap);
        if(mRecoverExtractor.getProgress() < 100
                && ui->goOnCheckBox->isChecked() ) {
            QTimer::singleShot(100, this, SLOT(on_stepButton_clicked()));
        }
    }
	ui->toolbarWidget->setEnabled(true);

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

			if(mFile.size() == 0) {
				mStatus = tr("Empty file ") + mFilename;
				return false;
			}
		} else {
			CPP_DELETE_ARRAY(mBufferRaw);
		}
	}
	if(!mDoubleJpegBuffer[0]) {
		for(int i = 0; i<2; ++i) {
			CPP_ALLOC_ARRAY(mDoubleJpegBuffer[i], unsigned char, mBufferMaxLen);
		}
	}

	if(mBufferRaw) {
		bool ok = mFile.seek(mLastPosition);
		mProgress = (int)(0.5f + 100.f *float(mFile.pos()) / float(mFile.size()));
		int readBytes = mFile.read((char *)mBufferRaw, mBufferMaxLen);
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
                             "MinStepSize=%d read=%d tag='0x%02x 0x%02x 0x%02x 0x%02x'",
				  mLastPosition,
				  mImageIndex, mImageSize,
				  readBytes,
                  mTag[0], mTag[1], mTag[2], mTag[3]);

		// We try to skip the start of the file to avoid the magic number
		// so the jpeg buffer might be found
		bool foundit = false;
		int found_at = 0;

		/***********************************************************************
		 *
		 * First pass, w edon't know the tag, so we do tiny steps of 1 byte
		 *
		 **********************************************************************/
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
				uint32_t tag32 = *(uint32_t *)(mBufferRaw + found_at);

                MSG_PRINT(LOG_DEBUG, "Current header: 1st=0x%04x =? cur=0x%04x",
                          mTag32, tag32);

				if(tag32 != mTag32) {
					MSG_PRINT(LOG_ERROR, "Not constant header: 1st=0x%04x != 2nd=0x%04x",
                              mTag32, tag32);
					mTag32 = 0; // So the search won't be accelerated
				}
			}
		} else {
			/***********************************************************************
			 *
			 * Accelerated pass, we already know the tag, so we look for it first
			 * then we check if te
			 *
			 **********************************************************************/
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
                    if(foundit) {
                        found_at = offset;
                    }
                }
				if(foundit) {
					MSG_PRINT(LOG_INFO, "Failback to normal=> found at %d", mLastPosition+offset);
                } else {
                    MSG_PRINT(LOG_INFO, "Not found => EOF=%c", mFile.atEnd()?'T':'F');
                }
			}
		}



		if(!foundit) {
			// No JPEG has been found => maybe it's the end of file ?
			if(mFile.atEnd()) {
				mStatus = tr("End of file, finished");
                MSG_PRINT(LOG_INFO, "END OF FILE => SAVE LAST");

                mProgress = 100;

				// Save last image
				// We need to update the data, but the image is already in buffer,
				// the writing is FoundAt (current) + previous increment
				// let's overwrite them
				int current_image = mDoubleJpegBufferIndex%2;
				int previous_image = (mDoubleJpegBufferIndex + 1)%2;
				mDoubleJpegBufferIncrement[previous_image] = 0;
				mDoubleJpegBufferFoundAt[current_image] = (mDoubleJpegBufferLastSize * 1.1f); // 10% margin
				mImageIndex++;
				//mDoubleJpegBufferIndex++; // to invert current and previous
				savePreviousImage();

				return true;
			}
			else {
				MSG_PRINT(LOG_ERROR, "No JPEG found, though it's not the end of file");
			}

			mStatus = tr("No jpeg found here");
			return false;
		} else {
			MSG_PRINT(LOG_DEBUG, "   => Found it at %d for ImageIndex=%d", mLastPosition + found_at, mImageIndex);

			// JPEG was found at byte # found_at from the beginning of the buffer
			mLastPosition += found_at;

			// for the next iteration, we will jump to this position + a margin, to not read the same image again
			int increment = found_at;

			if(mImageIndex == 1) {
				// once we are on the second image, wa know the size of the first image, so
				// so we can adapt the step to not read
				mImageSize = mLastPosition * 0.75f; // 3/4 of image size is the new increment
			//	mBufferMaxLen = std::min(MAX_JPEG_LEN, (int)(mLastPosition * 2.5f));
			} else {

			}


			// Save image
			mImageIndex++;
			MSG_PRINT(LOG_DEBUG, "    => Found JPG #%d at offset=%d",
					  mImageIndex,
					  mLastPosition);

            MSG_PRINT(LOG_DEBUG, "    => Found JPG #%d at offset=%d buffer=%x%x%x%x JFIF='0x%02x 0x%02x 0x%02x 0x%02x' tag='0x%02x 0x%02x 0x%02x 0x%02x'",
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
                      mTag[0], mTag[1], mTag[2], mTag[3]
					  );

			QString str;
			str.sprintf("Found JPG #%d at %.1f MB",
						mImageIndex,
						(float) mLastPosition / (1024.f*1024.f));

			// add an offset so we don't read again the same image
			int margin;
			if(mImageIndex <= 2) {
				margin= 10000; // min image size 200 kB for 2K video
			} else {
				margin = mImageSize;
			}
			increment += margin;
			mLastPosition += margin;

			mStatus = str;

			mLoadImage = loadImage.copy();

			// Save buffer for next time
			if(mDoubleJpegBuffer[0] && mDoubleJpegBuffer[1]) // alloc was fine, we can work with those buffers
			{
				/*
				 *
				 * Previous iteration
				 *
				 *
				 * Raw [----------------xJpegStart---------------(end somewhere)-------------] MaxLength
				 *                      | found_at
				 *                      <- increment -> x next fseek=found_at+increment
				 * => we copy in the double buffer, the raw buffer from found_at to the end.
				 *                      <--- copied in double budder ----------------------->
				 *
				 * next iteration: we jump in file by the increment,
				 * so the file pointer is at previous jpeg + increment
				 * the previous buffer contains the JPEG and some garbage
				 * At this iteration, we find the new found_at
				 *
				 * Raw [-----------xJpegStart---------------(end somewhere)-------------] MaxLength
				 *                 | found_at(N)
				 *
				 * So the size of the JPEG is the curret found_at + size of previous increment
				 *
				 */

				int current_index = mDoubleJpegBufferIndex%2;
				int previous_index = (mDoubleJpegBufferIndex+1)%2;
				mDoubleJpegBufferSize[current_index] = mBufferMaxLen - found_at;
				mDoubleJpegBufferIncrement[current_index] = increment;
				mDoubleJpegBufferFoundAt[current_index] = found_at;

				memcpy( mDoubleJpegBuffer[current_index],
						mBufferRaw + found_at,
						mDoubleJpegBufferSize[current_index] );

				mDoubleJpegBufferLastSize = found_at + mDoubleJpegBufferIncrement[previous_index];

				// Save current buffer
				savePreviousImage();

				mDoubleJpegBufferIndex++;


			} else {
				QString recoveredImageName;
				recoveredImageName.sprintf("REC_%04d.jpg", mImageIndex);
				MSG_PRINT(LOG_DEBUG, "Saving '%s'", qPrintable(recoveredImageName));

				QString imageFile = mDir.absoluteFilePath(recoveredImageName);

				// Stupid version: re-encode the JPEG file.
				ok = loadImage.save(imageFile, "JPG", 92);
				if(!ok) {
					mStatus = tr("Cannot save image ") + imageFile;
				}
			}
			// Try smarter: save the buffer, even if we save a few unused bytes
			// from the MJPEG encapsulation


			return ok;
		}
	}

	//CPP_DELETE_ARRAY(buffer);
	return false;
}

int RecoverExtractor::savePreviousImage()
{
	int current_index = mDoubleJpegBufferIndex%2;
	int previous_index = (mDoubleJpegBufferIndex+1)%2;

	// Save previous one
	QString recoveredImageName;
	recoveredImageName.sprintf("REC_%04d.jpg", mImageIndex-1);
	MSG_PRINT(LOG_DEBUG, "Saving buffered [prev=%d] {maxSize=%d, increment was %d, new found_at=%d} in '%s'",
			  previous_index,
			  mDoubleJpegBufferSize[previous_index],
			  mDoubleJpegBufferIncrement[previous_index],
			  mDoubleJpegBufferFoundAt[current_index],
			  qPrintable(recoveredImageName));

	QString imageFile = mDir.absoluteFilePath(recoveredImageName);
	if(mDoubleJpegBufferSize[previous_index] > 0) {
		FILE * f = fopen(qPrintable(imageFile), "wb");
		if(!f) {
			MSG_PRINT(LOG_ERROR, "Can't open file '%s' for writing ImageIndex %d",
					  qPrintable(imageFile),
					  mImageIndex);
			return -1;
		} else {
			// Save adapted buffer, we now know where is the next image
			MSG_PRINT(LOG_DEBUG, "Saving previous buffer = buffer [%d] of size %d limited by new found_at+prev_increment=%d",
					  previous_index,
					  mDoubleJpegBufferSize[previous_index],
					  mDoubleJpegBufferFoundAt[current_index] + mDoubleJpegBufferIncrement[previous_index]
					  );
			fwrite(mDoubleJpegBuffer[previous_index], 1,
				   mDoubleJpegBufferFoundAt[current_index] + mDoubleJpegBufferIncrement[previous_index],
				   f);
			fclose(f);
		}
	} else {
		if(mImageIndex>0) {
			MSG_PRINT(LOG_ERROR, "0 size buffer for ImageIndex %d", mImageIndex);
			return -1;
		}
	}
	return 0;
}



static bool s_debug_alloc = false;
void registerAlloc(const char * file, const char *func, int line,
				   void * buf, size_t size) {
    if(!s_debug_alloc) { return; }
    fprintf(stderr, "%s:%s:%d: allocate %p / %zu bytes\n", file, func, line, buf, size);
}

void registerDelete(const char * file, const char *func, int line,
					void * buf) {
    if(!s_debug_alloc) { return; }
    fprintf(stderr, "%s:%s:%d: delete %p\n", file, func, line, buf);
}

