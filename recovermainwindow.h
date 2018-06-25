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

#ifndef RECOVERMAINWINDOW_H
#define RECOVERMAINWINDOW_H

#include <QMainWindow>
#include <QFile>
#include <QDir>
#include <QString>
#include <QImage>

namespace Ui {
class RecoverMainWindow;
}

/*! \brief Log level */
typedef enum {
	LOG_CRITICAL,
	LOG_ERROR,
	LOG_WARNING,
	LOG_INFO,
	LOG_DEBUG,
	LOG_TRACE
} te_log_level;

extern te_log_level g_log_level;
#define MSG_PRINT(_lvl, ...)	do { if((_lvl) <= g_log_level) { \
									fprintf(stdout, "%s:%d: ", __func__, __LINE__); \
									fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); fflush(stdout); \
								}} while(0)

/* MEMORY ALLOCATIONS MACROS */
#define CPP_ALLOC(_var, _type)	(_var) = new _type; \
								registerAlloc(__FILE__, __func__, __LINE__, \
									(void*)(_var), sizeof((_type)));
#define REGISTER_ALLOC(_var, _size)	registerAlloc(__FILE__, __func__, __LINE__, \
									(void*)(_var), (_size));
#define CPP_ALLOC_ARRAY(_var, _type, _size)	(_var) = new _type [ (_size) ]; \
								registerAlloc(__FILE__, __func__, __LINE__, \
									(void*)(_var), sizeof(_type) * ((_size)));
#define REGISTER_ALLOC_ARRAY(_var, _size)	registerAlloc(__FILE__, __func__, __LINE__, \
									(void*)(_var), (_size));
#define CPP_DELETE(_var)		registerDelete(__FILE__, __func__, __LINE__, \
										(void*)(_var)); \
								if((_var)) { delete (_var); }
#define CPP_DELETE_ARRAY(_var)		registerDelete(__FILE__, __func__, __LINE__, \
										(void*)(_var)); \
								if((_var)) { delete [] (_var); }
#define REGISTER_DELETE(_var, _size)	registerDelete(__FILE__, __func__, __LINE__, \
									(void*)(_var), (_size));

/*! \brief Track memory allocations */
void registerAlloc(const char * file, const char *func, int line,
				   void * buf, size_t size);

/*! \brief Track memory delete */
void registerDelete(const char * file, const char *func, int line,
				   void * buf);


/// Max jpeg length for 4K on DxO One
#define MAX_JPEG_LEN 7000000


/*! \brief Extractor class to extract data from the file */
class RecoverExtractor : public QObject {
	Q_OBJECT
public:
	RecoverExtractor();
	~RecoverExtractor();

	/// \brief Set MOV input file name
	void setFilename(const QString & filename);

	/// \brief Extract one frame
	bool extract();

	/// \brief Get status string
	QString getStatus() { return mStatus; }

	/// \brief Get progress in %
	int getProgress() { return mProgress; }

	QImage getImage() { return mLoadImage; }

private:
	void init();
	void purge();

	/// \brief Current file name
	QString mFilename;

	/// \brief Current directory
	QDir mDir;

	/// \brief Current status
	QString mStatus;

	/// \brief Last position in file, for reading next frame
	int mLastPosition;

	/// \brief Progress in %
	int mProgress;

	/// \brief Index of recovered imafe
	int mImageIndex;

	/// \brief Hint of image minimal size (3/4 of first frame)
	int mImageSize;

	/// \brief Reading buffer
	uint8_t * mBufferRaw;

	/// \brief Size of buffer read iteration
	int mBufferMaxLen;

	QFile mFile;		///< Current file, once open
	uint8_t mTag[5];	///< 4 first chars of the searched JPEG buffer
	uint32_t mTag32;	///< unsigned int 32bit version of the \see tag

	QImage mLoadImage;	///< Last read image
};


/*! \brief Main program header */
class RecoverMainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit RecoverMainWindow(QWidget *parent = 0);
	~RecoverMainWindow();

private slots:
	void on_openButton_clicked();
	void on_stepButton_clicked();

private:
	Ui::RecoverMainWindow *ui;
	void loadSettings();
	void saveSettings();

	RecoverExtractor mRecoverExtractor;
	/// \brief Path of last directory
	QString mLastDir;
};


#endif // RECOVERMAINWINDOW_H
