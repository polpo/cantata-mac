/*
 * Cantata
 *
 * Copyright (c) 2011-2013 Craig Drummond <craig.p.drummond@gmail.com>
 *
 * ----
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "actiondialog.h"
#include "devicesmodel.h"
#include "device.h"
#include "utils.h"
#include "devicepropertiesdialog.h"
#include "devicepropertieswidget.h"
#include "settings.h"
#include "musiclibrarymodel.h"
#include "musiclibraryproxymodel.h"
#include "dirviewmodel.h"
#include "albumsmodel.h"
#include "mpdparseutils.h"
#include "mpdconnection.h"
#include "encoders.h"
#include "localize.h"
#include "messagebox.h"
#include "filejob.h"
#include "freespaceinfo.h"
#include "icons.h"
#include "config.h"
#include "tags.h"
#include "treeview.h"
#include "onlineservicesmodel.h"
#ifdef ENABLE_REPLAYGAIN_SUPPORT
#include "rgdialog.h"
#endif
#include <QFile>
#include <QDebug>

static int iCount=0;

int ActionDialog::instanceCount()
{
    return iCount;
}

enum Pages
{
    PAGE_START,
    PAGE_ERROR,
    PAGE_SKIP,
    PAGE_PROGRESS
};

class SongDialog : public Dialog
{
public:
    SongDialog(ActionDialog *p)
        : Dialog(p) {
        setCaption(i18n("Songs To Be Copied"));
        setButtons(Close);
        MusicLibraryModel *model=new MusicLibraryModel(this, false);
        MusicLibraryProxyModel *proxy=new MusicLibraryProxyModel(this);
        proxy->setSourceModel(model);
        model->setUseAlbumImages(false);
        model->setUseArtistImages(false);
        model->setSupportsAlbumArtistTag(true);
        TreeView *view=new TreeView(this);
        view->setPageDefaults();
        view->setExpandOnClick();
        view->setModel(proxy);
        view->setUseSimpleDelegate();
        model->update(p->songsToAction.toSet());
        setMainWidget(view);
        int size=fontMetrics().height();
        int numArtists=model->rowCount();
        resize(20*size, qMin(qMax(10, numArtists)+4, 25)*(size*1.25));
    }
};

ActionDialog::ActionDialog(QWidget *parent)
    : Dialog(parent)
    , mpdConfigured(false)
    , currentDev(0)
    , songDialog(0)
{
    iCount++;
    setButtons(Ok|Cancel);
    setDefaultButton(Cancel);
    setAttribute(Qt::WA_DeleteOnClose);
    QWidget *mainWidet = new QWidget(this);
    setupUi(mainWidet);
    setMainWidget(mainWidet);
    errorIcon->setPixmap(QIcon::fromTheme("dialog-error").pixmap(64, 64));
    skipIcon->setPixmap(QIcon::fromTheme("dialog-warning").pixmap(64, 64));
    configureSourceButton->setIcon(Icons::self()->configureIcon);
    configureDestButton->setIcon(Icons::self()->configureIcon);
    connect(configureSourceButton, SIGNAL(clicked()), SLOT(configureSource()));
    connect(configureDestButton, SIGNAL(clicked()), SLOT(configureDest()));
    connect(this, SIGNAL(update()), MPDConnection::self(), SLOT(update()));
    connect(songCount, SIGNAL(leftClickedUrl()), SLOT(showSongs()));
}

ActionDialog::~ActionDialog()
{
    iCount--;
}

void ActionDialog::controlInfoLabel(Device *dev)
{
    DeviceOptions opts;
    if (sourceIsAudioCd) {
        opts.load(MPDConnectionDetails::configGroupName(MPDConnection::self()->getDetails().name), true);
    } else if (sourceUdi.isEmpty()) {
        opts=dev->options();
    }

    if (opts.transcoderCodec.isEmpty()) {
        codecLabel->setVisible(false);
        codec->setVisible(false);
    } else {
        codecLabel->setVisible(true);
        codec->setVisible(true);
        Encoders::Encoder encoder=Encoders::getEncoder(opts.transcoderCodec);
        if (Encoders::getEncoder(opts.transcoderCodec).codec.isEmpty()) {
            codec->setText(i18n("<b>INVALID</b>"));
        } else if (encoder.values.count()>1) {
            int settingIndex=0;
            bool increase=encoder.values.at(0).value<encoder.values.at(1).value;
            int index=0;
            foreach (const Encoders::Setting &s, encoder.values) {
                if ((increase && s.value>opts.transcoderValue) || (!increase && s.value<opts.transcoderValue)) {
                    break;
                } else {
                    settingIndex=index;
                }
                index++;
            }
            codec->setText(QString("%1 (%2)").arg(encoder.name).arg(encoder.values.at(settingIndex).descr)+
                           (sourceIsAudioCd && opts.transcoderWhenDifferent ? QLatin1String("")+i18n("<i>(When different)</i>") : QString()));
        } else {
            codec->setText(encoder.name+
                           (sourceIsAudioCd && opts.transcoderWhenDifferent ? QLatin1String("")+i18n("<i>(When different)</i>") : QString()));
        }
    }
}

void ActionDialog::showSongs()
{
    if (!songDialog) {
        songDialog=new SongDialog(this);
    }
    songDialog->show();
}

void ActionDialog::hideSongs()
{
    if (songDialog) {
        songDialog->setVisible(false);
        songDialog->deleteLater();
        songDialog=0;
    }
}

void ActionDialog::updateSongCountLabel()
{
    QSet<QString> artists;
    QSet<QString> albums;

    foreach (const Song &s, songsToAction) {
        artists.insert(s.albumArtist());
        albums.insert(s.albumArtist()+"--"+s.album);
    }

    songCount->setText(i18n("Artists:%1, Albums:%2, Songs:%3", artists.count(), albums.count(), songsToAction.count()));
}

void ActionDialog::controlInfoLabel()
{
    controlInfoLabel(getDevice(sourceUdi.isEmpty() ? destUdi : sourceUdi));
}

void ActionDialog::copy(const QString &srcUdi, const QString &dstUdi, const QList<Song> &songs)
{
    init(srcUdi, dstUdi, songs, Copy);
    Device *dev=getDevice(sourceUdi.isEmpty() ? destUdi : sourceUdi);

    if (!dev) {
        deleteLater();
        return;
    }

    sourceIsAudioCd=Device::AudioCd==dev->devType();
    controlInfoLabel(dev);

    // check space...
    haveVariousArtists=false;
    qint64 spaceRequired=0;
    #ifdef ACTION_DIALOG_SHOW_TIME_REMAINING
    totalTime=0;
    #endif
    foreach (const Song &s, songsToAction) {
        if (s.size>0) {
            spaceRequired+=s.size;
        }
        if (!haveVariousArtists && s.isVariousArtists()) {
            haveVariousArtists=true;
        }
        #ifdef ACTION_DIALOG_SHOW_TIME_REMAINING
        totalTime+=s.time;
        #endif
    }

    qint64 spaceAvailable=0;
    double usedCapacity=0.0;
    QString capacityString;
    //bool isFromOnline=sourceUdi.startsWith(OnlineServicesModel::constUdiPrefix);

    if (sourceUdi.isEmpty()) { // If source UDI is empty, then we are copying from MPD->device, so need device free space.
        spaceAvailable=dev->freeSpace();
        capacityString=dev->capacityString();
        usedCapacity=dev->usedCapacity();
    } else {
        FreeSpaceInfo inf=FreeSpaceInfo(MPDConnection::self()->getDetails().dir);
        spaceAvailable=inf.size()-inf.used();
        usedCapacity=(inf.used()*1.0)/(inf.size()*1.0);
        capacityString=i18n("%1 free", Utils::formatByteSize(inf.size()-inf.used()));
    }

    bool enoughSpace=spaceAvailable>spaceRequired;
    #ifdef ENABLE_REMOTE_DEVICES
    if (!enoughSpace && sourceUdi.isEmpty() && 0==spaceAvailable && usedCapacity<0.0 && Device::RemoteFs==dev->devType()) {
        enoughSpace=true;
    }
    #endif
    if (enoughSpace || (sourceUdi.isEmpty() && Encoders::getAvailable().count())) {
        QString mpdCfgName=MPDConnectionDetails::configGroupName(MPDConnection::self()->getDetails().name);
        overwrite->setChecked(Settings::self()->overwriteSongs());
        sourceLabel->setText(QLatin1String("<b>")+(sourceUdi.isEmpty()
                                ? i18n("Local Music Library")
                                : sourceIsAudioCd
                                    ? i18n("Audio CD")
                                    : dev->data())+QLatin1String("</b>"));
        destinationLabel->setText(QLatin1String("<b>")+(destUdi.isEmpty() ? i18n("Local Music Library") : dev->data())+QLatin1String("</b>"));
        namingOptions.load(mpdCfgName, true);
        setPage(PAGE_START);
        mode=Copy;

        capacity->update(capacityString, (usedCapacity*100)+0.5);

        bool destIsDev=sourceUdi.isEmpty();
        mpdConfigured=DeviceOptions::isConfigured(mpdCfgName, true);
        configureDestLabel->setVisible((destIsDev && !dev->isConfigured()) || (!destIsDev && !mpdConfigured));
        //configureSourceButton->setVisible(!isFromOnline);
        //configureSourceLabel->setVisible(!isFromOnline && ((!destIsDev && !dev->isConfigured()) || (destIsDev && !mpdConfigured)));
        configureSourceButton->setVisible(false);
        configureSourceLabel->setVisible(false);
        songCount->setVisible(!sourceIsAudioCd);
        songCountLabel->setVisible(!sourceIsAudioCd);
        if (!sourceIsAudioCd) {
            updateSongCountLabel();
        }
        show();
        if (!enoughSpace) {
            MessageBox::information(this, i18n("There is insufficient space left on the destination device.\n"
                                               "The selected songs consume %1, but there is only %2 left.\n"
                                               "The songs will need to be transcoded to a smaller filesize in order to be successfully copied.",
                                               Utils::formatByteSize(spaceRequired),
                                               Utils::formatByteSize(spaceAvailable)));
        }
    } else {
        MessageBox::error(parentWidget(), i18n("There is insufficient space left on the destination.\n"
                                               "The selected songs consume %1, but there is only %2 left.",
                                               Utils::formatByteSize(spaceRequired),
                                               Utils::formatByteSize(spaceAvailable)));
        deleteLater();
    }
}

void ActionDialog::remove(const QString &udi, const QList<Song> &songs)
{
    init(udi, QString(), songs, Remove);
    codecLabel->setVisible(false);
    codec->setVisible(false);
    songCountLabel->setVisible(false);
    songCount->setVisible(false);
    QString baseDir;

    if (udi.isEmpty()) {
        baseDir=MPDConnection::self()->getDetails().dir;
    } else {
        Device *dev=getDevice(udi);

        if (!dev) {
            deleteLater();
            return;
        }

        baseDir=dev->path();
    }

    setPage(PAGE_PROGRESS);
    foreach (const Song &s, songsToAction) {
        dirsToClean.insert(baseDir+Utils::getDir(s.file));
    }
    show();
    #ifdef ACTION_DIALOG_SHOW_TIME_REMAINING
    timer.start();
    #endif
    doNext();
}

void ActionDialog::init(const QString &srcUdi, const QString &dstUdi, const QList<Song> &songs, Mode m)
{
    resize(500, 160);
    sourceUdi=srcUdi;
    destUdi=dstUdi;
    sourceIsAudioCd=false;
    songsToAction=songs;
    mode=m;
    setCaption(Copy==mode ? i18n("Copy Songs") : i18n("Delete Songs"));
    qSort(songsToAction);
    progressLabel->setText(QString());
    progressBar->setValue(0);
    progressBar->setRange(0, (Copy==mode ? songsToAction.count() : (songsToAction.count()+1))*100);
    autoSkip=false;
    performingAction=false;
    paused=false;
    actionedSongs.clear();
    skippedSongs.clear();
    #ifdef ACTION_DIALOG_SHOW_TIME_REMAINING
    actionedTime=0;
    timeTaken=0;
    #endif
    currentPercent=0;
    currentDev=0;
    count=0;
    #ifdef ENABLE_REPLAYGAIN_SUPPORT
    albumsWithoutRgTags.clear();
    #endif
}

void ActionDialog::slotButtonClicked(int button)
{
    switch(stack->currentIndex()) {
    case PAGE_START:
        switch (button) {
        case Ok:
            if (haveVariousArtists &&
                ((configureDestLabel->isVisible() && sourceUdi.isEmpty() && // Only warn if copying FROM library
                  MessageBox::No==MessageBox::warningYesNo(this, i18n("<p>You have not configured the destination device.<br/>"
                                                                      "Continue with the default settings?</p>"), i18n("Not Configured"),
                                                           GuiItem(i18n("Use Defaults")), StdGuiItem::cancel())) ||
                 (configureSourceLabel->isVisible() && !sourceUdi.isEmpty() && // Only warn if copying TO library
                  MessageBox::No==MessageBox::warningYesNo(this, i18n("<p>You have not configured the source device.<br/>"
                                                                      "Continue with the default settings?</p>"), i18n("Not Configured"),
                                                           GuiItem(i18n("Use Defaults")), StdGuiItem::cancel())) ) ) {
                return;
            }
            Settings::self()->saveOverwriteSongs(overwrite->isChecked());
            setPage(PAGE_PROGRESS);
            hideSongs();
            #ifdef ACTION_DIALOG_SHOW_TIME_REMAINING
            timer.start();
            #endif
            doNext();
            break;
        case Cancel:
            refreshLibrary();
            reject();
            // Need to call this - if not, when dialog is closed by window X control, it is not deleted!!!!
            Dialog::slotButtonClicked(button);
            break;
        default:
            Dialog::slotButtonClicked(button);
            break;
        }
        break;
    case PAGE_SKIP:
        #ifdef ACTION_DIALOG_SHOW_TIME_REMAINING
        timeTaken+=timer.elapsed();
        #endif
        setPage(PAGE_PROGRESS);
        switch(button) {
        case User1:
            skippedSongs.append(currentSong);
            #ifdef ACTION_DIALOG_SHOW_TIME_REMAINING
            totalTime-=currentSong.time;
            timer.restart();
            #endif
            incProgress();
            doNext();
            break;
        case User2:
            autoSkip=true;
            incProgress();
            doNext();
            break;
        case User3:
            songsToAction.prepend(origCurrentSong);
            doNext();
            break;
        default:
            refreshLibrary();
            reject();
            // Need to call this - if not, when dialog is closed by window X control, it is not deleted!!!!
            Dialog::slotButtonClicked(button);
            break;
        }
        break;
    case PAGE_ERROR:
        refreshLibrary();
        reject();
        break;
    case PAGE_PROGRESS:
        paused=true;
        if (MessageBox::Yes==MessageBox::questionYesNo(this, i18n("Are you sure you wish to stop?"), i18n("Stop"),
                                                       StdGuiItem::stop(), StdGuiItem::cont())) {
            Device *dev=0;
            if(Copy==mode) {
                dev=getDevice(sourceUdi.isEmpty() ? destUdi : sourceUdi, false);
            } else if (!sourceUdi.isEmpty()) { // Must be a delete...
                dev=getDevice(sourceUdi, false);
            }

            if (dev) {
                if (Close==button) { // Close is only enabled when saving cache...
                    disconnect(dev, SIGNAL(cacheSaved()), this, SLOT(cacheSaved()));
                } else {
                    dev->abortJob();
                }
            }
            if (Close!=button) {
                refreshLibrary();
            }
            reject();
            // Need to call this - if not, when dialog is closed by window X control, it is not deleted!!!!
            Dialog::slotButtonClicked(button);
        } else if (!performingAction && PAGE_PROGRESS==stack->currentIndex()) {
            paused=false;
            incProgress();
            doNext();
        }
    }
}

Device * ActionDialog::getDevice(const QString &udi, bool logErrors)
{
    if (udi.startsWith(OnlineServicesModel::constUdiPrefix)) {
        return OnlineServicesModel::self()->device(udi);
    }
    Device *dev=DevicesModel::self()->device(udi);

    if (!logErrors) {
        return dev;
    }

    QString error;
    if (!dev) {
        error=i18n("Device has been removed!");
    } else if (!dev->isConnected()) {
        error=i18n("Device is not connected!");
    } else if (!dev->isIdle()) {
        error=i18n("Device is busy?");
    } else if (currentDev && dev!=currentDev) {
        error=i18n("Device has been changed?");
    }

    if (error.isEmpty()) {
        return dev;
    }

    if (isVisible()) {
        setPage(PAGE_ERROR, error);
    } else {
        MessageBox::error(parentWidget(), error);
    }

    return 0;
}

void ActionDialog::doNext()
{
    currentPercent=0;
    if (songsToAction.count()) {
        currentSong=origCurrentSong=songsToAction.takeFirst();
        if(Copy==mode) {
            bool copyToDev=sourceUdi.isEmpty();
            Device *dev=getDevice(copyToDev ? destUdi : sourceUdi);

            if (dev) {
                if (!currentDev) {
                    connect(dev, SIGNAL(actionStatus(int, bool)), this, SLOT(actionStatus(int, bool)));
                    connect(dev, SIGNAL(progress(int)), this, SLOT(jobPercent(int)));
                    currentDev=dev;
                }
                performingAction=true;
                if (copyToDev) {
                    destFile=dev->path()+dev->options().createFilename(currentSong);
                    currentSong.file=MPDConnection::self()->getDetails().dir+currentSong.file;
                    dev->addSong(currentSong, overwrite->isChecked(), !copiedCovers.contains(Utils::getDir(destFile)));
                } else {
                    Song copy=currentSong;
                    if (dev->options().fixVariousArtists && currentSong.isVariousArtists()) {
                        Device::fixVariousArtists(QString(), copy, false);
                    }
                    QString fileName=namingOptions.createFilename(copy);
                    destFile=MPDConnection::self()->getDetails().dir+fileName;
                    dev->copySongTo(currentSong, MPDConnection::self()->getDetails().dir, fileName, overwrite->isChecked(), !copiedCovers.contains(Utils::getDir(destFile)));
                }
            }
        } else {
            if (sourceUdi.isEmpty()) {
                performingAction=true;
                currentSong.file=MPDConnection::self()->getDetails().dir+currentSong.file;
                removeSong(currentSong);
            } else {
                Device *dev=getDevice(sourceUdi);
                if (dev) {
                    if (dev!=currentDev) {
                        connect(dev, SIGNAL(actionStatus(int)), this, SLOT(actionStatus(int)));
                        currentDev=dev;
                    }
                    performingAction=true;
                    dev->removeSong(currentSong);
                }
            }
        }
        progressLabel->setText(formatSong(currentSong, false, true));
    } else if (Remove==mode && dirsToClean.count()) {
        Device *dev=sourceUdi.isEmpty() ? 0 : DevicesModel::self()->device(sourceUdi);
        if (sourceUdi.isEmpty() || dev) {
            progressLabel->setText(i18n("Clearing unused folders"));
            if (dev) {
                dev->cleanDirs(dirsToClean);
            } else {
                cleanDirs();
            }
        }
        dirsToClean.clear();
    } else {
        if (!refreshLibrary()) {
            emit completed();
            accept();
            #ifdef ENABLE_REPLAYGAIN_SUPPORT
            if (Copy==mode && !albumsWithoutRgTags.isEmpty() && sourceIsAudioCd) {
                QWidget *pw=parentWidget();
                if (MessageBox::Yes==MessageBox::questionYesNo(pw, i18n("Calculate ReplayGain for ripped tracks?"), i18n("ReplyGain"),
                                                               GuiItem(i18n("Calculate")), StdGuiItem::no())) {
                    RgDialog *dlg=new RgDialog(pw);
                    QList<Song> songs;

                    foreach (const Song &s, actionedSongs) {
                        if (albumsWithoutRgTags.contains(s.album)) {
                            Song song=s;
                            song.file=namingOptions.createFilename(s);
                            songs.append(song);
                        }
                    }
                    dlg->show(songs, QString(), true);
                }
            }
            #endif
        }
    }
}

void ActionDialog::actionStatus(int status, bool copiedCover)
{
    int origStatus=status;
    bool wasSkip=false;
    if (Device::Ok!=status && Device::NotConnected!=status && autoSkip) {
        skippedSongs.append(currentSong);
        #ifdef ACTION_DIALOG_SHOW_TIME_REMAINING
        totalTime-=currentSong.time;
        #endif
        wasSkip=true;
        status=Device::Ok;
    }
    switch (status) {
    case Device::Ok:
        performingAction=false;
        if (Device::Ok==origStatus) {
            if (!wasSkip) {
                actionedSongs.append(currentSong);
                #ifdef ACTION_DIALOG_SHOW_TIME_REMAINING
                actionedTime+=currentSong.time;
                #endif
                #ifdef ENABLE_REPLAYGAIN_SUPPORT
                if (Copy==mode && sourceIsAudioCd && !albumsWithoutRgTags.contains(currentSong.album) && Tags::readReplaygain(destFile).isEmpty()) {
                    albumsWithoutRgTags.insert(currentSong.album);
                }
                #endif
            }
            if (copiedCover) {
                copiedCovers.insert(Utils::getDir(destFile));
            }
        }
        if (!paused) {
            incProgress();
            doNext();
        }
        break;
    case Device::FileExists:
        setPage(PAGE_SKIP, i18n("The destination filename already exists!<hr/>%1", formatSong(currentSong, true)));
        break;
    case Device::SongExists:
        setPage(PAGE_SKIP, i18n("Song already exists!<hr/>%1", formatSong(currentSong)));
        break;
    case Device::SongDoesNotExist:
        setPage(PAGE_SKIP, i18n("Song does not exist!<hr/>%1", formatSong(currentSong)));
        break;
    case Device::DirCreationFaild:
        setPage(PAGE_SKIP, i18n("Failed to create destination folder!<br/>Please check you have sufficient permissions.<hr/>%1", formatSong(currentSong, true)));
        break;
    case Device::SourceFileDoesNotExist:
        setPage(PAGE_SKIP, i18n("Source file no longer exists?<br/><br/<hr/>%1", formatSong(currentSong, true)));
        break;
    case Device::Failed:
        setPage(PAGE_SKIP, Copy==mode ? i18n("Failed to copy.<hr/>%1", formatSong(currentSong))
                                      : i18n("Failed to delete.<hr/>%1", formatSong(currentSong)));
        break;
    case Device::NotConnected:
        setPage(PAGE_ERROR, i18n("Not connected to device.<hr/>%1", formatSong(currentSong)));
        break;
    case Device::CodecNotAvailable:
        setPage(PAGE_ERROR, i18n("Selected codec is not available.<hr/>%1", formatSong(currentSong)));
        break;
    case Device::TranscodeFailed:
        setPage(PAGE_SKIP, i18n("Transcoding failed.<br/><br/<hr/>%1", formatSong(currentSong)));
        break;
    case Device::FailedToCreateTempFile:
        setPage(PAGE_ERROR, i18n("Failed to create temporary file.<br/>(Required for transcoding to MTP devices.)<hr/>%1", formatSong(currentSong)));
        break;
    case Device::ReadFailed:
        setPage(PAGE_SKIP, i18n("Failed to read source file.<br/><br/<hr/>%1", formatSong(currentSong)));
        break;
    case Device::WriteFailed:
        setPage(PAGE_SKIP, i18n("Failed to write to destination file.<br/><br/<hr/>%1", formatSong(currentSong)));
        break;
    case Device::NoSpace:
        setPage(PAGE_SKIP, i18n("No space left on device.<br/><br/<hr/>%1", formatSong(currentSong)));
        break;
    case Device::FailedToUpdateTags:
        setPage(PAGE_SKIP, i18n("Failed to update metadata.<br/><br/<hr/>%1", formatSong(currentSong)));
        break;
    case Device::TooManyRedirects:
        setPage(PAGE_SKIP, i18n("Failed to download track - too many redirects encountered.<br/><br/<hr/>%1", formatSong(currentSong)));
        break;
    case Device::DownloadFailed:
        setPage(PAGE_SKIP, i18n("Failed to download track.<br/><br/<hr/>%1", formatSong(currentSong)));
        break;
    case Device::FailedToLockDevice:
        setPage(PAGE_ERROR, i18n("Failed to lock device.<hr/>%1", formatSong(currentSong)));
        break;
    case Device::Cancelled:
        break;
    default:
        break;
    }
}

void ActionDialog::configureDest()
{
    configureDestLabel->setVisible(false);
    configure(destUdi);
}

void ActionDialog::configureSource()
{
    configureSourceLabel->setVisible(false);
    configure(sourceUdi);
}

void ActionDialog::configure(const QString &udi)
{
    if (udi.isEmpty()) {
        DevicePropertiesDialog *dlg=new DevicePropertiesDialog(this);
        connect(dlg, SIGNAL(updatedSettings(const QString &, const DeviceOptions &)), SLOT(saveProperties(const QString &, const DeviceOptions &)));
        if (!mpdConfigured) {
            connect(dlg, SIGNAL(cancelled()), SLOT(saveProperties()));
        }
        dlg->setCaption(i18n("Local Music Library Properties"));
        dlg->show(MPDConnection::self()->getDetails().dir, namingOptions, DevicePropertiesWidget::Prop_Basic|(sourceIsAudioCd ? DevicePropertiesWidget::Prop_Encoder : 0));
        connect(dlg, SIGNAL(destroyed()), SLOT(controlInfoLabel()));
    } else {
        Device *dev=DevicesModel::self()->device(udi);
        if (dev) {
            dev->configure(this);
            connect(dev, SIGNAL(configurationChanged()), SLOT(controlInfoLabel()));
        }
    }
}

void ActionDialog::saveProperties(const QString &path, const DeviceOptions &opts)
{
    Q_UNUSED(path)
    namingOptions=opts;
    namingOptions.save(MPDConnectionDetails::configGroupName(MPDConnection::self()->getDetails().name), true, sourceIsAudioCd);
    mpdConfigured=true;
}

void ActionDialog::saveProperties()
{
    namingOptions.save(MPDConnectionDetails::configGroupName(MPDConnection::self()->getDetails().name), true, sourceIsAudioCd);
    mpdConfigured=true;
}

void ActionDialog::setPage(int page, const QString &msg)
{
    stack->setCurrentIndex(page);

    switch(page)
    {
        case PAGE_START:
            setButtons(Ok|Cancel);
            break;
        case PAGE_PROGRESS:
            actionLabel->startAnimation();
            setButtons(Cancel);
            break;
        case PAGE_SKIP:
            actionLabel->stopAnimation();
            skipText->setText(i18n("<b>Error</b><br/>")+msg);
            skipText->setToolTip(formatSong(currentSong, true));
            if (songsToAction.count()) {
                setButtons(Cancel|User1|User2|User3);
                setButtonText(User1, i18n("Skip"));
                setButtonText(User2, i18n("Auto Skip"));
            } else {
                setButtons(Cancel|User3);
            }
            setButtonText(User3, i18n("Retry"));
            break;
        case PAGE_ERROR:
            actionLabel->stopAnimation();
            stack->setCurrentIndex(PAGE_ERROR);
            errorText->setText(i18n("<b>Error</b><br/>")+msg);
            errorText->setToolTip(formatSong(currentSong, true));
            setButtons(Cancel);
            break;
    }
}

QString ActionDialog::formatSong(const Song &s, bool showFiles, bool showTime)
{
    QString str("<table>");
    str+=i18n("<tr><td align=\"right\">Artist:</td><td>%1</td></tr>"
              "<tr><td align=\"right\">Album:</td><td>%2</td></tr>"
              "<tr><td align=\"right\">Track:</td><td>%3</td></tr>",
              s.albumArtist(), s.album, s.trackAndTitleStr(Song::isVariousArtists(s.albumArtist()) && !Song::isVariousArtists(s.artist)));

    if (showFiles) {
        if (Copy==mode) {
            str+=i18n("<tr><td align=\"right\">Source file:</td><td>%1</td></tr>"
                      "<tr><td align=\"right\">Destination file:</td><td>%2</td></tr>",
                      DevicesModel::fixDevicePath(s.file), DevicesModel::fixDevicePath(destFile));

        } else {
            str+=i18n("<tr><td align=\"right\">File:</td><td>%1</td></tr>", DevicesModel::fixDevicePath(s.file));
        }
    }

    #ifdef ACTION_DIALOG_SHOW_TIME_REMAINING
    if (showTime) {
        QString estimate=i18n("Calculating...");
        double taken=timeTaken+timer.elapsed();
        if (taken>5.0) {
            double pc=(progressBar->value()*1.0)/(progressBar->maximum()*1.0);
            double percent=Copy==mode ? (actionedTime+(currentPercent*0.01*currentSong.time))/totalTime : pc;
            percent-=percent*((1.0-percent)*0.15);
            quint64 timeRemaining=((taken/percent)-taken)/1000.0;
            estimate=i18nc("time (Estimated)", "%1 (Estimated)", Song::formattedTime(timeRemaining>0 ? timeRemaining : 0));
        }

        str+=i18n("<tr><i><td align=\"right\"><i>Time remaining:</i></td><td><i>%5</i></td></i></tr>", estimate);
    }
    #else
    Q_UNUSED(showTime)
    #endif
    
    return str+"</table>";
}

bool ActionDialog::refreshLibrary()
{
    actionLabel->stopAnimation();
    if (!actionedSongs.isEmpty()) {
        if ( (Copy==mode && !sourceUdi.isEmpty()) ||
             (Remove==mode && sourceUdi.isEmpty()) ) {
            AlbumsModel::self()->update(MusicLibraryModel::self()->root());
            emit update();
        } else if ( (Copy==mode && sourceUdi.isEmpty()) ||
                    (Remove==mode && !sourceUdi.isEmpty()) ) {
            Device *dev=DevicesModel::self()->device(sourceUdi.isEmpty() ? destUdi : sourceUdi);

            if (dev) {
                dev->saveCache();
                progressLabel->setText(i18n("Saving cache"));
                connect(dev, SIGNAL(cacheSaved()), this, SLOT(cacheSaved()));
                setButtons(Close);
                return true;
            }
        }
    }
    return false;
}

void ActionDialog::removeSong(const Song &s)
{
    if (!QFile::exists(s.file)) {
        actionStatus(Device::SourceFileDoesNotExist);
        return;
    }

    DeleteJob *job=new DeleteJob(s.file, true);
    connect(job, SIGNAL(result(int)), SLOT(removeSongResult(int)));
    job->start();
}

void ActionDialog::removeSongResult(int status)
{
    FileJob::finished(sender());
    if (Device::Ok!=status) {
        actionStatus(status);
    } else {
        MusicLibraryModel::self()->removeSongFromList(currentSong);
        DirViewModel::self()->removeFileFromList(currentSong.file);
        actionStatus(Device::Ok);
    }
}

void ActionDialog::cleanDirs()
{
    CleanJob *job=new CleanJob(dirsToClean, MPDConnection::self()->getDetails().dir, QString());
    connect(job, SIGNAL(result(int)), SLOT(cleanDirsResult(int)));
    connect(job, SIGNAL(percent(int)), SLOT(jobPercent(int)));
    job->start();
}

void ActionDialog::cleanDirsResult(int status)
{
    FileJob::finished(sender());
    actionStatus(status);
}

void ActionDialog::jobPercent(int percent)
{
    if (percent!=currentPercent) {
        progressBar->setValue((100*count)+percent);
        currentPercent=percent;
        if (PAGE_PROGRESS==stack->currentIndex()) {
            progressLabel->setText(formatSong(currentSong, false, true));
        }
    }
}

void ActionDialog::incProgress()
{
    count++;
    progressBar->setValue(100*count);
}

void ActionDialog::cacheSaved()
{
    emit completed();
    accept();
}
