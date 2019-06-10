/*************************************************************************
 *  Copyright (C) 2008,2009 by Volker Lanz <vl@fidra.de>                 *
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

#ifndef KPMCORE_LINUXSWAP_H
#define KPMCORE_LINUXSWAP_H

#include "util/libpartitionmanagerexport.h"

#include "fs/filesystem.h"

class Report;

namespace FS
{
/** A linux swap pseudo file system.
    @author Volker Lanz <vl@fidra.de>
 */
class LIBKPMCORE_EXPORT linuxswap : public FileSystem
{
public:
    linuxswap(qint64 firstsector, qint64 lastsector, qint64 sectorsused, const QString& label);

public:
    void init() override;

    bool create(Report& report, const QString& deviceNode) override;
    bool resize(Report& report, const QString& deviceNode, qint64 length) const override;
    bool writeLabel(Report& report, const QString& deviceNode, const QString& newLabel) override;
    bool writeLabelOnline(Report& report, const QString& deviceNode, const QString& mountPoint, const QString& newLabel) override;
    bool copy(Report& report, const QString& targetDeviceNode, const QString& sourceDeviceNode) const override;
    bool updateUUID(Report& report, const QString& deviceNode) const override;
    qint64 readUsedCapacity(const QString& deviceNode) const override;

    bool canMount(const QString& deviceNode, const QString& mountPoint) const override;
    bool mount(Report& report, const QString& deviceNode, const QString& mountPoint) override;
    bool unmount(Report& report, const QString& deviceNode) override;

    QString mountTitle() const override;
    QString unmountTitle() const override;

    CommandSupportType supportCreate() const override {
        return m_Create;
    }
    CommandSupportType supportGrow() const override {
        return m_Grow;
    }
    CommandSupportType supportShrink() const override {
        return m_Shrink;
    }
    CommandSupportType supportMove() const override {
        return m_Move;
    }
    CommandSupportType supportCopy() const override {
        return m_Copy;
    }
    CommandSupportType supportGetUsed() const override {
        return m_GetUsed;
    }
    CommandSupportType supportGetLabel() const override {
        return m_GetLabel;
    }
    CommandSupportType supportSetLabel() const override {
        return m_SetLabel;
    }
    CommandSupportType supportSetLabelOnline() const override {
        return m_SetLabel;
    }
    CommandSupportType supportUpdateUUID() const override {
        return m_UpdateUUID;
    }
    CommandSupportType supportGetUUID() const override {
        return m_GetUUID;
    }

    int maxLabelLength() const override;
    SupportTool supportToolName() const override;
    bool supportToolFound() const override;

public:
    static CommandSupportType m_Create;
    static CommandSupportType m_Grow;
    static CommandSupportType m_Shrink;
    static CommandSupportType m_Move;
    static CommandSupportType m_Copy;
    static CommandSupportType m_SetLabel;
    static CommandSupportType m_GetLabel;
    static CommandSupportType m_GetUsed;
    static CommandSupportType m_UpdateUUID;
    static CommandSupportType m_GetUUID;
};
}

#endif
