/*
    SPDX-FileCopyrightText: 2008-2010 Volker Lanz <vl@fidra.de>
    SPDX-FileCopyrightText: 2008 Laurent Montel <montel@kde.org>
    SPDX-FileCopyrightText: 2014-2018 Andrius Štikonas <andrius@stikonas.eu>
    SPDX-FileCopyrightText: 2015 Chris Campbell <c.j.campbell@ed.ac.uk>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef KPMCORE_OPERATION_H
#define KPMCORE_OPERATION_H

#include "util/libpartitionmanagerexport.h"

#include <QObject>
#include <QList>
#include <QtGlobal>

#include <memory>

class Partition;
class Device;
class Job;
class OperationPrivate;
class OperationStack;
class OperationRunner;
class Report;

class QString;
class QIcon;

/** Base class of all Operations.

    An Operation serves two purposes: It is responsible for modifying the device preview to show the
    user a state as if the Operation had already been applied and it is made up of Jobs to actually
    perform what the Operation is supposed to do.

    Most Operations just run a list of Jobs and for that reason do not even overwrite
    Operation::execute(). The more complex Operations, however, need to perform some
    extra tasks in between running Jobs (most notably RestoreOperation and CopyOperation). These do
    overwrite Operation::execute().

    Operations own the objects they deal with in most cases, usually Partitions. But as soon as
    an Operation has been successfully executed, it no longer owns anything, because the
    OperationStack then takes over ownership.

    Some rules for creating new operations that inherit the Operation class:

    <ol>
        <li>
            Don't modify anything in the ctor. The ctor runs before merging operations. If you
            modify anything there, undo and merging will break. Just remember what you're
            supposed to do in the ctor and perform modifications in preview().
        </li>
        <li>
            Do not access the preview partitions and devices in description(). If you do,
            the operation descriptions will be wrong.
        </li>
        <li>
            Don't create or delete objects in preview() or undo() since these will be called
            more than once. Create and delete objects in the ctor and dtor.
        </li>
    </ol>

    @author Volker Lanz <vl@fidra.de>
*/
class LIBKPMCORE_EXPORT Operation : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(Operation)

    friend class OperationStack;
    friend class OperationRunner;

public:
    /** Status of this Operation */
    enum OperationStatus {
        StatusNone = 0,             /**< None yet, can be merged */
        StatusPending,              /**< Pending, can be undone */
        StatusRunning,              /**< Currently running */
        StatusFinishedSuccess,      /**< Successfully finished */
        StatusFinishedWarning,      /**< Finished with warnings */
        StatusError                 /**< Finished with errors */
    };

protected:
    Operation();
    ~Operation() override;

Q_SIGNALS:
    void progress(int);
    void jobStarted(Job*, Operation*);
    void jobFinished(Job*, Operation*);

public:
    virtual QString iconName() const = 0; /**< @return name of the icon for the Operation */
    virtual QString description() const = 0; /**< @return the Operation's description */
    virtual void preview() = 0; /**< Apply the Operation to the current preview */
    virtual void undo() = 0; /**< Undo applying the Operation to the current preview */
    virtual bool execute(Report& parent);

    virtual bool targets(const Device&) const = 0;
    virtual bool targets(const Partition&) const = 0;

    /**< @return the current status */
    virtual OperationStatus status() const;

    virtual QString statusText() const;
    virtual QString statusIcon() const;

    /**< @param s the new status */
    virtual void setStatus(OperationStatus s);

    qint32 totalProgress() const;

protected:
    void onJobStarted();
    void onJobFinished();

    void insertPreviewPartition(Device& targetDevice, Partition& newPartition);
    void removePreviewPartition(Device& device, Partition& p);

    void addJob(Job* job);

    QList<Job*>& jobs();
    const QList<Job*>& jobs() const;

    void setProgressBase(qint32 i);
    qint32 progressBase() const;

private:
    std::unique_ptr<OperationPrivate> d;
};

#endif
