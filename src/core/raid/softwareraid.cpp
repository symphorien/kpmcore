/*************************************************************************
 *  Copyright (C) 2018 by Caio Carvalho <caiojcarvalho@gmail.com>        *
 *                                                                       *
 *  This program is free software; you can redistribute it and/or        *
 *  modify it under the terms of the GNU General Public License as       *
 *  published by the Free Software Foundation; either version 3 of       *
 *  the License, or (at your option) any later version.                  *
 *                                                                       *
 *  This program is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of       *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        *
 *  GNU General Public License for more details.                         *
 *                                                                       *
 *  You should have received a copy of the GNU General Public License    *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.*
 *************************************************************************/

#include "softwareraid.h"

#include "backend/corebackend.h"
#include "backend/corebackendmanager.h"
#include "core/partition.h"
#include "core/volumemanagerdevice_p.h"
#include "fs/filesystem.h"
#include "fs/filesystemfactory.h"
#include "util/externalcommand.h"

#include <KLocalizedString>
#include <QRegularExpression>

#define d_ptr std::static_pointer_cast<SoftwareRAIDPrivate>(d)

class SoftwareRAIDPrivate : public VolumeManagerDevicePrivate
{
public:
    qint32 m_raidLevel;
    qint64 m_chunkSize;
    qint64 m_totalChunk;
    qint64 m_arraySize;
    QString m_UUID;
    QStringList m_devicePathList;
    SoftwareRAID::Status m_status;
};

SoftwareRAID::SoftwareRAID(const QString& name, SoftwareRAID::Status status, const QString& iconName)
    : VolumeManagerDevice(std::make_shared<SoftwareRAIDPrivate>(),
                          name,
                          (QStringLiteral("/dev/") + name),
                          getChunkSize(QStringLiteral("/dev/") + name),
                          getTotalChunk(QStringLiteral("/dev/") + name),
                          iconName,
                          Device::Type::SoftwareRAID_Device)
{
    d_ptr->m_raidLevel = getRaidLevel(deviceNode());
    d_ptr->m_chunkSize = logicalSize();
    d_ptr->m_totalChunk = totalLogical();
    d_ptr->m_arraySize = getArraySize(deviceNode());
    d_ptr->m_UUID = getUUID(deviceNode());
    d_ptr->m_devicePathList = getDevicePathList(deviceNode());
    d_ptr->m_status = status;

    initPartitions();
}

const QStringList SoftwareRAID::deviceNodes() const
{
    return d_ptr->m_devicePathList;
}

const QStringList& SoftwareRAID::partitionNodes() const
{
    return {};
}

qint64 SoftwareRAID::partitionSize(QString &partitionPath) const
{
    Q_UNUSED(partitionPath)
    return 0;
}

bool SoftwareRAID::growArray(Report &report, const QStringList &devices)
{
    Q_UNUSED(report)
    Q_UNUSED(devices)
    return false;
}

bool SoftwareRAID::shrinkArray(Report &report, const QStringList &devices)
{
    Q_UNUSED(report)
    Q_UNUSED(devices)
    return false;
}

QString SoftwareRAID::prettyName() const
{
    QString raidInfo;

    if (status() != SoftwareRAID::Status::Inactive)
        raidInfo = xi18nc("@item:inlistbox [RAID level]", " [RAID %1]", raidLevel());
    else
        raidInfo = QStringLiteral(" [RAID]");

    return VolumeManagerDevice::prettyName() + raidInfo;
}

bool SoftwareRAID::operator ==(const Device& other) const
{
    bool equalDeviceNode = Device::operator ==(other);

    if (other.type() == Device::Type::SoftwareRAID_Device) {
        const SoftwareRAID& raid = static_cast<const SoftwareRAID&>(other);

        if (!equalDeviceNode)
            return raid.uuid() == uuid();
    }

    return equalDeviceNode;
}

qint32 SoftwareRAID::raidLevel() const
{
    return d_ptr->m_raidLevel;
}

qint64 SoftwareRAID::chunkSize() const
{
    return d_ptr->m_chunkSize;
}

qint64 SoftwareRAID::totalChunk() const
{
    return d_ptr->m_totalChunk;
}

qint64 SoftwareRAID::arraySize() const
{
    return d_ptr->m_arraySize;
}

QString SoftwareRAID::uuid() const
{
    return d_ptr->m_UUID;
}

QStringList SoftwareRAID::devicePathList() const
{
    return d_ptr->m_devicePathList;
}

SoftwareRAID::Status SoftwareRAID::status() const
{
    return d_ptr->m_status;
}

void SoftwareRAID::setStatus(SoftwareRAID::Status status)
{
    d_ptr->m_status = status;
}

void SoftwareRAID::scanSoftwareRAID(QList<Device*>& devices)
{
    QList<Device *> scannedRaid;

    // TODO: Support custom config files.
    QString config = getRAIDConfiguration(QStringLiteral("/etc/mdadm.conf"));

    if (!config.isEmpty()) {
        QRegularExpression re(QStringLiteral("([\\t\\r\\n\\f\\s]|INACTIVE-)ARRAY \\/dev\\/([\\/\\w-]+)"));
        QRegularExpressionMatchIterator i  = re.globalMatch(config);

        while (i.hasNext()) {
            QRegularExpressionMatch reMatch = i.next();
            QString deviceName = reMatch.captured(2).trimmed();

            SoftwareRAID *raidDevice = new SoftwareRAID(deviceName,
                                                        SoftwareRAID::Status::Inactive);

            scannedRaid << raidDevice;
        }
    }

    ExternalCommand scanRaid(QStringLiteral("cat"), { QStringLiteral("/proc/mdstat") });

    if (scanRaid.run(-1) && scanRaid.exitCode() == 0) {
        QRegularExpression re(QStringLiteral("md([\\/\\w]+)\\s+:\\s+([\\w]+)"));
        QRegularExpressionMatchIterator i  = re.globalMatch(scanRaid.output());
        while (i.hasNext()) {
            QRegularExpressionMatch reMatch = i.next();

            QString deviceNode = QStringLiteral("/dev/md") + reMatch.captured(1).trimmed();
            QString status = reMatch.captured(2).trimmed();

            SoftwareRAID* d = static_cast<SoftwareRAID *>(CoreBackendManager::self()->backend()->scanDevice(deviceNode));

            if (scannedRaid.contains(d)) {
                if (status == QStringLiteral("inactive"))
                    d->setStatus(SoftwareRAID::Status::Inactive);
            }
            else
                scannedRaid << d;

            if (d->raidLevel() > 0) {
                QRegularExpression reMirrorStatus(QStringLiteral("\\[[=>.]+\\]\\s+(resync|recovery)"));

                QRegularExpressionMatch reMirrorStatusMatch = reMirrorStatus.match(scanRaid.output());

                if (reMirrorStatusMatch.hasMatch()) {
                    if (reMirrorStatusMatch.captured(1) == QStringLiteral("resync"))
                        d->setStatus(SoftwareRAID::Status::Resync);
                    else if (reMirrorStatusMatch.captured(1) == QStringLiteral("recovery"))
                        d->setStatus(SoftwareRAID::Status::Recovery);
                }
            }
        }
    }

    devices << scannedRaid;
}

qint32 SoftwareRAID::getRaidLevel(const QString &path)
{
    QString output = getDetail(path);

    if (!output.isEmpty()) {
        QRegularExpression re(QStringLiteral("Raid Level :\\s+\\w+(\\d+)"));
        QRegularExpressionMatch reMatch = re.match(output);
        if (reMatch.hasMatch())
            return reMatch.captured(1).toLongLong();
    }

    return -1;
}

qint64 SoftwareRAID::getChunkSize(const QString &path)
{
    QString output = getDetail(path);
    if (!output.isEmpty()) {
        QRegularExpression re(QStringLiteral("Chunk Size :\\s+(\\d+)"));
        QRegularExpressionMatch reMatch = re.match(output);
        if (reMatch.hasMatch())
            return reMatch.captured(1).toLongLong();
    }
    return -1;

}

qint64 SoftwareRAID::getTotalChunk(const QString &path)
{
    return getArraySize(path) / getChunkSize(path);
}

qint64 SoftwareRAID::getArraySize(const QString &path)
{
    QString output = getDetail(path);
    if (!output.isEmpty()) {
        QRegularExpression re(QStringLiteral("Array Size :\\s+(\\d+)"));
        QRegularExpressionMatch reMatch = re.match(output);
        if (reMatch.hasMatch())
            return reMatch.captured(1).toLongLong() * 1024;
    }
    return -1;

}

QString SoftwareRAID::getUUID(const QString &path)
{
    QString output = getDetail(path);

    if (!output.isEmpty()) {
        QRegularExpression re(QStringLiteral("UUID :\\s+([\\w:]+)"));
        QRegularExpressionMatch reMatch = re.match(output);

        if (reMatch.hasMatch())
            return reMatch.captured(1);
    }

    // If UUID was not found in detail output, it should be searched in config file

    // TODO: Support custom config files.
    QString config = getRAIDConfiguration(QStringLiteral("/etc/mdadm.conf"));

    if (!config.isEmpty()) {
        QRegularExpression re(QStringLiteral("([\\t\\r\\n\\f\\s]|INACTIVE-)ARRAY \\/dev\\/md([\\/\\w-]+)(.*)"));
        QRegularExpressionMatchIterator i  = re.globalMatch(config);

        while (i.hasNext()) {
            QRegularExpressionMatch reMatch = i.next();
            QString deviceNode = QStringLiteral("/dev/md") + reMatch.captured(2).trimmed();
            QString otherInfo = reMatch.captured(3).trimmed();

            // Consider device node as name=host:deviceNode when the captured device node string has '-' character
            // It happens when user have included the device to config file using 'mdadm --examine --scan'
            if (deviceNode.contains(QLatin1Char('-'))) {
                QRegularExpression reName(QStringLiteral("name=[\\w:]+\\/dev\\/md\\/([\\/\\w]+)"));
                QRegularExpressionMatch nameMatch = reName.match(otherInfo);

                if (nameMatch.hasMatch())
                    deviceNode = nameMatch.captured(1);
            }

            if (deviceNode == path) {
                QRegularExpression reUUID(QStringLiteral("(UUID=|uuid=)([\\w:]+)"));
                QRegularExpressionMatch uuidMatch = reUUID.match(otherInfo);

                if (uuidMatch.hasMatch())
                    return uuidMatch.captured(2);
            }
        }
    }

    return QString();
}

QStringList SoftwareRAID::getDevicePathList(const QString &path)
{
    Q_UNUSED(path)
    return {};
}

bool SoftwareRAID::isRaidPath(const QString &path)
{
    return !getDetail(path).isEmpty();
}

bool SoftwareRAID::createSoftwareRAID(Report &report,
                                      const QString &name,
                                      const QStringList devicePathList,
                                      const qint32 raidLevel,
                                      const qint32 chunkSize)
{
    return false;
}

bool SoftwareRAID::deleteSoftwareRAID(Report &report,
                                      SoftwareRAID &raidDevice)
{
    Q_UNUSED(report)
    Q_UNUSED(raidDevice)
    return false;
}

bool SoftwareRAID::assembleSoftwareRAID(const QString& deviceNode)
{
    if (!isRaidPath(deviceNode))
        return false;

    ExternalCommand cmd(QStringLiteral("mdadm"),
                        { QStringLiteral("--assemble"), QStringLiteral("--scan"), deviceNode });

    return cmd.run(-1) && cmd.exitCode() == 0;
}

bool SoftwareRAID::stopSoftwareRAID(const QString& deviceNode)
{
    if (!isRaidPath(deviceNode))
        return false;

    ExternalCommand cmd(QStringLiteral("mdadm"),
                        { QStringLiteral("--manage"), QStringLiteral("--stop"), deviceNode });

    return cmd.run(-1) && cmd.exitCode() == 0;
}

bool SoftwareRAID::reassembleSoftwareRAID(const QString &deviceNode)
{
    return stopSoftwareRAID(deviceNode) && assembleSoftwareRAID(deviceNode);
}

void SoftwareRAID::initPartitions()
{

}

qint64 SoftwareRAID::mappedSector(const QString &partitionPath, qint64 sector) const
{
    return -1;
}

QString SoftwareRAID::getDetail(const QString &path)
{
    ExternalCommand cmd(QStringLiteral("mdadm"),
                       { QStringLiteral("--misc"), QStringLiteral("--detail"), path });
    return (cmd.run(-1) && cmd.exitCode() == 0) ? cmd.output() : QString();
}

QString SoftwareRAID::getRAIDConfiguration(const QString &configurationPath)
{
    ExternalCommand cmd(QStringLiteral("cat"), { configurationPath });

    return (cmd.run(-1) && cmd.exitCode() == 0) ? cmd.output() : QString();
}
