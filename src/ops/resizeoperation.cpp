/*
    SPDX-FileCopyrightText: 2008-2012 Volker Lanz <vl@fidra.de>
    SPDX-FileCopyrightText: 2012-2020 Andrius Štikonas <andrius@stikonas.eu>
    SPDX-FileCopyrightText: 2015 Teo Mrnjavac <teo@kde.org>
    SPDX-FileCopyrightText: 2016 Chantara Tith <tith.chantara@gmail.com>
    SPDX-FileCopyrightText: 2018 Caio Jordão Carvalho <caiojcarvalho@gmail.com>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ops/resizeoperation.h"

#include "core/partition.h"
#include "core/device.h"
#include "core/lvmdevice.h"
#include "core/partitiontable.h"
#include "core/copysourcedevice.h"
#include "core/copytargetdevice.h"

#include "jobs/checkfilesystemjob.h"
#include "jobs/setpartgeometryjob.h"
#include "jobs/resizefilesystemjob.h"
#include "jobs/movefilesystemjob.h"

#include "ops/checkoperation.h"

#include "fs/filesystem.h"
#include "fs/luks.h"

#include "util/capacity.h"
#include "util/report.h"

#include <QDebug>
#include <QString>

#include <KLocalizedString>

/** Creates a new ResizeOperation.
    @param d the Device to resize a Partition on
    @param p the Partition to resize
    @param newfirst the new first sector of the Partition
    @param newlast the new last sector of the Partition
*/
ResizeOperation::ResizeOperation(Device& d, Partition& p, qint64 newfirst, qint64 newlast) :
    Operation(),
    m_TargetDevice(d),
    m_Partition(p),
    m_OrigFirstSector(partition().firstSector()),
    m_OrigLastSector(partition().lastSector()),
    m_NewFirstSector(newfirst),
    m_NewLastSector(newlast),
    m_CheckOriginalJob(new CheckFileSystemJob(partition())),
    m_MoveExtendedJob(nullptr),
    m_ShrinkResizeJob(nullptr),
    m_ShrinkSetGeomJob(nullptr),
    m_MoveSetGeomJob(nullptr),
    m_MoveFileSystemJob(nullptr),
    m_GrowResizeJob(nullptr),
    m_GrowSetGeomJob(nullptr),
    m_CheckResizedJob(nullptr)
{
    if (CheckOperation::canCheck(&partition()))
        addJob(checkOriginalJob());

    if (partition().roles().has(PartitionRole::Extended)) {
        m_MoveExtendedJob = new SetPartGeometryJob(targetDevice(), partition(), newFirstSector(), newLength());
        addJob(moveExtendedJob());
    } else {
        if (resizeAction() & Shrink) {
            m_ShrinkResizeJob = new ResizeFileSystemJob(targetDevice(), partition(), newLength());
            m_ShrinkSetGeomJob = new SetPartGeometryJob(targetDevice(), partition(), partition().firstSector(), newLength());

            addJob(shrinkResizeJob());
            addJob(shrinkSetGeomJob());
        }

        if ((resizeAction() & MoveLeft) || (resizeAction() & MoveRight)) {
            // At this point, we need to set the partition's length to either the resized length, if it has already been
            // shrunk, or to the original length (it may or may not then later be grown, we don't care here)
            const qint64 currentLength = (resizeAction() & Shrink) ? newLength() : partition().length();

            m_MoveSetGeomJob = new SetPartGeometryJob(targetDevice(), partition(), newFirstSector(), currentLength);
            m_MoveFileSystemJob = new MoveFileSystemJob(targetDevice(), partition(), newFirstSector());

            addJob(moveSetGeomJob());
            addJob(moveFileSystemJob());
        }

        if (resizeAction() & Grow) {
            m_GrowSetGeomJob = new SetPartGeometryJob(targetDevice(), partition(), newFirstSector(), newLength());
            m_GrowResizeJob = new ResizeFileSystemJob(targetDevice(), partition(), newLength());

            addJob(growSetGeomJob());
            addJob(growResizeJob());
        }

        m_CheckResizedJob = new CheckFileSystemJob(partition());

        if(CheckOperation::canCheck(&partition()))
            addJob(checkResizedJob());
    }
}

bool ResizeOperation::targets(const Device& d) const
{
    return d == targetDevice();
}

bool ResizeOperation::targets(const Partition& p) const
{
    return p == partition();
}

void ResizeOperation::preview()
{
    // If the operation has already been executed, the partition will of course have newFirstSector and
    // newLastSector as first and last sector. But to remove it from its original position, we need to
    // temporarily set these values back to where they were before the operation was executed.
    if (partition().firstSector() == newFirstSector() && partition().lastSector() == newLastSector()) {
        partition().setFirstSector(origFirstSector());
        partition().setLastSector(origLastSector());
    }

    removePreviewPartition(targetDevice(), partition());

    partition().setFirstSector(newFirstSector());
    partition().setLastSector(newLastSector());

    insertPreviewPartition(targetDevice(), partition());
}

void ResizeOperation::undo()
{
    removePreviewPartition(targetDevice(), partition());
    partition().setFirstSector(origFirstSector());
    partition().setLastSector(origLastSector());
    insertPreviewPartition(targetDevice(), partition());
}

bool ResizeOperation::execute(Report& parent)
{
    bool rval = true;

    Report* report = parent.newChild(description());

    if (CheckOperation::canCheck(&partition()))
        rval = checkOriginalJob()->run(*report);

    if (rval) {
        // Extended partitions are a special case: They don't have any file systems and so there's no
        // need to move, shrink or grow their contents before setting the new geometry. In fact, trying
        // to first shrink THEN move would not work for an extended partition that has children, because
        // they might temporarily be outside the extended partition and the backend would not let us do that.
        if (moveExtendedJob()) {
            if (!(rval = moveExtendedJob()->run(*report)))
                report->line() << xi18nc("@info:status", "Moving extended partition <filename>%1</filename> failed.", partition().deviceNode());
        } else {
            // We run all three methods. Any of them returns true if it has nothing to do.
            rval = shrink(*report) && move(*report) && grow(*report);

            if (rval) {
                if (CheckOperation::canCheck(&partition())) {
                    rval = checkResizedJob()->run(*report);
                    if (!rval)
                        report->line() << xi18nc("@info:status", "Checking partition <filename>%1</filename> after resize/move failed.", partition().deviceNode());
                }
            } else
                report->line() << xi18nc("@info:status", "Resizing/moving partition <filename>%1</filename> failed.", partition().deviceNode());
        }
    } else
        report->line() << xi18nc("@info:status", "Checking partition <filename>%1</filename> before resize/move failed.", partition().deviceNode());

    setStatus(rval ? StatusFinishedSuccess : StatusError);

    report->setStatus(xi18nc("@info:status (success, error, warning...) of operation", "%1: %2", description(), statusText()));

    return rval;
}

QString ResizeOperation::description() const
{
    // There are eight possible things a resize operation might do:
    // 1) Move a partition to the left (closer to the start of the disk)
    // 2) Move a partition to the right (closer to the end of the disk)
    // 3) Grow a partition
    // 4) Shrink a partition
    // 5) Move a partition to the left and grow it
    // 6) Move a partition to the right and grow it
    // 7) Move a partition to the left and shrink it
    // 8) Move a partition to the right and shrink it
    // Each of these needs a different description. And for reasons of i18n, we cannot
    // just concatenate strings together...

    const QString moveDelta = Capacity::formatByteSize(qAbs(newFirstSector() - origFirstSector()) * targetDevice().logicalSize());

    const QString origCapacity = Capacity::formatByteSize(origLength() * targetDevice().logicalSize());
    const QString newCapacity = Capacity::formatByteSize(newLength() * targetDevice().logicalSize());

    switch (resizeAction()) {
    case MoveLeft:
        return xi18nc("@info:status describe resize/move action", "Move partition <filename>%1</filename> to the left by %2", partition().deviceNode(), moveDelta);

    case MoveRight:
        return xi18nc("@info:status describe resize/move action", "Move partition <filename>%1</filename> to the right by %2", partition().deviceNode(), moveDelta);

    case Grow:
        return xi18nc("@info:status describe resize/move action", "Grow partition <filename>%1</filename> from %2 to %3", partition().deviceNode(), origCapacity, newCapacity);

    case Shrink:
        return xi18nc("@info:status describe resize/move action", "Shrink partition <filename>%1</filename> from %2 to %3", partition().deviceNode(), origCapacity, newCapacity);

    case MoveLeftGrow:
        return xi18nc("@info:status describe resize/move action", "Move partition <filename>%1</filename> to the left by %2 and grow it from %3 to %4", partition().deviceNode(), moveDelta, origCapacity, newCapacity);

    case MoveRightGrow:
        return xi18nc("@info:status describe resize/move action", "Move partition <filename>%1</filename> to the right by %2 and grow it from %3 to %4", partition().deviceNode(), moveDelta, origCapacity, newCapacity);

    case MoveLeftShrink:
        return xi18nc("@info:status describe resize/move action", "Move partition <filename>%1</filename> to the left by %2 and shrink it from %3 to %4", partition().deviceNode(), moveDelta, origCapacity, newCapacity);

    case MoveRightShrink:
        return xi18nc("@info:status describe resize/move action", "Move partition <filename>%1</filename> to the right by %2 and shrink it from %3 to %4", partition().deviceNode(), moveDelta, origCapacity, newCapacity);

    case None:
        qWarning() << "Could not determine what to do with partition " << partition().deviceNode() << ".";
        break;
    }

    return xi18nc("@info:status describe resize/move action", "Unknown resize/move action.");
}

ResizeOperation::ResizeAction ResizeOperation::resizeAction() const
{
    ResizeAction action = None;

    // Grow?
    if (newLength() > origLength())
        action = Grow;

    // Shrink?
    if (newLength() < origLength())
        action = Shrink;

    // Move to the right?
    if (newFirstSector() > origFirstSector())
        action = static_cast<ResizeAction>(action | MoveRight);

    // Move to the left?
    if (newFirstSector() < origFirstSector())
        action = static_cast<ResizeAction>(action | MoveLeft);

    return action;
}

bool ResizeOperation::shrink(Report& report)
{
    if (shrinkResizeJob() && !shrinkResizeJob()->run(report)) {
        report.line() << xi18nc("@info:status", "Resize/move failed: Could not resize file system to shrink partition <filename>%1</filename>.", partition().deviceNode());
        return false;
    }

    if (shrinkSetGeomJob() && !shrinkSetGeomJob()->run(report)) {
        report.line() << xi18nc("@info:status", "Resize/move failed: Could not shrink partition <filename>%1</filename>.", partition().deviceNode());
        return false;

        /** @todo if this fails, no one undoes the shrinking of the file system above, because we
        rely upon there being a maximize job at the end, but that's no longer the case. */
    }

    return true;
}

bool ResizeOperation::move(Report& report)
{
    // We must make sure not to overwrite the partition's metadata if it's a logical partition
    // and we're moving to the left. The easiest way to achieve this is to move the
    // partition itself first (it's the backend's responsibility to then move the metadata) and
    // only afterwards copy the filesystem. Disadvantage: We need to move the partition
    // back to its original position if copyBlocks fails.
    const qint64 oldStart = partition().firstSector();
    if (moveSetGeomJob() && !moveSetGeomJob()->run(report)) {
        report.line() << xi18nc("@info:status", "Moving partition <filename>%1</filename> failed.", partition().deviceNode());
        return false;
    }

    if (moveFileSystemJob() && !moveFileSystemJob()->run(report)) {
        report.line() << xi18nc("@info:status", "Moving the filesystem for partition <filename>%1</filename> failed. Rolling back.", partition().deviceNode());

        // see above: We now have to move back the partition itself.
        if (!SetPartGeometryJob(targetDevice(), partition(), oldStart, partition().length()).run(report))
            report.line() << xi18nc("@info:status", "Moving back partition <filename>%1</filename> to its original position failed.", partition().deviceNode());

        return false;
    }

    return true;
}

bool ResizeOperation::grow(Report& report)
{
    const qint64 oldLength = partition().length();

    if (growSetGeomJob() && !growSetGeomJob()->run(report)) {
        report.line() << xi18nc("@info:status", "Resize/move failed: Could not grow partition <filename>%1</filename>.", partition().deviceNode());
        return false;
    }

    if (growResizeJob() && !growResizeJob()->run(report)) {
        report.line() << xi18nc("@info:status", "Resize/move failed: Could not resize the file system on partition <filename>%1</filename>", partition().deviceNode());

        if (!SetPartGeometryJob(targetDevice(), partition(), partition().firstSector(), oldLength).run(report))
            report.line() << xi18nc("@info:status", "Could not restore old partition size for partition <filename>%1</filename>.", partition().deviceNode());

        return false;
    }

    return true;
}

/** Can a Partition be grown, i.e. increased in size?
    @param p the Partition in question, may be nullptr.
    @return true if @p p can be grown.
 */
bool ResizeOperation::canGrow(const Partition* p)
{
    if (p == nullptr)
        return false;

    // Whole block device filesystems cannot be resized
    if (p->partitionTable()->type() == PartitionTable::TableType::none)
        return false;

    if (isLVMPVinNewlyVG(p))
        return false;

    // we can always grow, shrink or move a partition not yet written to disk
    if (p->state() == Partition::State::New && !p->roles().has(PartitionRole::Luks))
        return true;

    if (p->isMounted())
        return p->fileSystem().supportGrowOnline();

    return p->fileSystem().supportGrow() != FileSystem::cmdSupportNone;
}

/** Can a Partition be shrunk, i.e. decreased in size?
    @param p the Partition in question, may be nullptr.
    @return true if @p p can be shrunk.
 */
bool ResizeOperation::canShrink(const Partition* p)
{
    if (p == nullptr)
        return false;

    // Whole block device filesystems cannot be resized
    if (p->partitionTable()->type() == PartitionTable::TableType::none)
        return false;

    if (isLVMPVinNewlyVG(p))
        return false;

    // we can always grow, shrink or move a partition not yet written to disk
    if (p->state() == Partition::State::New && !p->roles().has(PartitionRole::Luks))
        return true;

    if (p->state() == Partition::State::Copy)
        return false;

    if (p->isMounted())
        return p->fileSystem().supportShrinkOnline();

    return p->fileSystem().supportShrink() != FileSystem::cmdSupportNone;
}

/** Can a Partition be moved?
    @param p the Partition in question, may be nullptr.
    @return true if @p p can be moved.
 */
bool ResizeOperation::canMove(const Partition* p)
{
    if (p == nullptr)
        return false;

    // Whole block device filesystems cannot be moved
    if (p->partitionTable()->type() == PartitionTable::TableType::none)
        return false;

    if (isLVMPVinNewlyVG(p))
        return false;

    // we can always grow, shrink or move a partition not yet written to disk
    if (p->state() == Partition::State::New)
        // too many bad things can happen for LUKS partitions
        return p->roles().has(PartitionRole::Luks) ? false : true;

    if (p->isMounted())
        return false;

    // no moving of extended partitions if they have logicals
    if (p->roles().has(PartitionRole::Extended) && p->hasChildren())
        return false;

    return p->fileSystem().supportMove() != FileSystem::cmdSupportNone;
}

bool ResizeOperation::isLVMPVinNewlyVG(const Partition *p)
{
    if (p->fileSystem().type() == FileSystem::Type::Lvm2_PV) {
        if (LvmDevice::s_DirtyPVs.contains(p))
            return true;
    }
    else if (p->fileSystem().type() == FileSystem::Type::Luks || p->fileSystem().type() == FileSystem::Type::Luks2) {
        // See if innerFS is LVM
        FileSystem *fs = static_cast<const FS::luks *>(&p->fileSystem())->innerFS();

        if (fs) {
            if (fs->type() == FileSystem::Type::Lvm2_PV) {
                if (LvmDevice::s_DirtyPVs.contains(p))
                    return true;
            }
        }
    }

    return false;
}
