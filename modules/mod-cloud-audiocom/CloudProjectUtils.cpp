/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  CloudProjectUtils.cpp

  Dmitry Vedenko

**********************************************************************/
#include "CloudProjectUtils.h"

#include <wx/log.h>

#include "AuthorizationHandler.h"
#include "BasicUI.h"
#include "CloudProjectFileIOExtensions.h"
#include "CloudSyncService.h"
#include "CodeConversions.h"
#include "Project.h"
#include "ProjectFileIO.h"
#include "ProjectManager.h"
#include "ProjectWindow.h"
#include "ServiceConfig.h"
#include "UriParser.h"

#include "ui/dialogs/LinkFailedDialog.h"
#include "ui/dialogs/ProjectVersionConflictDialog.h"
#include "ui/dialogs/SyncFailedDialog.h"
#include "ui/dialogs/UnsyncedProjectDialog.h"

#include "sync/CloudSyncDTO.h"
#include "sync/MixdownUploader.h"
#include "sync/ProjectCloudExtension.h"

namespace audacity::cloud::audiocom::sync
{
namespace
{
AudacityProject* GetPotentialTarget()
{
   return AllProjects {}.empty() ? nullptr : AllProjects {}.rbegin()->get();
}

auto MakeProgress()
{
   return BasicUI::MakeProgress(
      XO("Open from audio.com"), XO("Synchronizing project"),
      BasicUI::ProgressShowCancel);
}

auto MakePoller(BasicUI::ProgressDialog& dialog)
{
   return [&dialog](double progress)
   {
      return dialog.Poll(static_cast<unsigned>(progress * 10000), 10000ULL) ==
             BasicUI::ProgressResult::Success;
   };
}

template<typename T>
T GetResult(std::future<T>& future)
{
   while (future.wait_for(std::chrono::milliseconds(50)) !=
          std::future_status::ready)
      BasicUI::Yield();

   return future.get();
}

bool HandleFailure(const ProjectSyncResult& result)
{
   if (
      result.Status == ProjectSyncResult::StatusCode::Succeeded ||
      result.Result.Code == SyncResultCode::Conflict)
      return false;

   if (result.Result.Code == SyncResultCode::Cancelled)
   {
      wxLogError(
         "Opening of the cloud project was canceled", result.Result.Content);
      return true;
   }

   if (result.Result.Code == SyncResultCode::SyncImpossible)
   {
      UnsyncedProjectDialog { nullptr, false }.ShowDialog();
      return true;
   }

   SyncFailedDialog::OnOpen(result.Result);

   wxLogError("Failed to open cloud project: %s", result.Result.Content);

   return true;
}

enum class ConflictResolution
{
   None,
   Remote,
   Local,
   Stop
};

ConflictResolution
GetConfilctResolution(AudacityProject* project, const ProjectSyncResult& result)
{
   if (result.Result.Code != SyncResultCode::Conflict)
      return ConflictResolution::None;

   ProjectVersionConflictDialog dialog { project, false };
   const auto resolution = dialog.ShowDialog();

   if (resolution == ProjectVersionConflictDialog::CancellButtonIdentifier())
      return ConflictResolution::Stop;

   if (resolution == ProjectVersionConflictDialog::UseLocalIdentifier())
      return ConflictResolution::Local;

   if (resolution == ProjectVersionConflictDialog::UseRemoteIdentifier())
      return ConflictResolution::Remote;

   assert(false);
   return ConflictResolution::Stop;
}

void LogTransferStats(TransferStats stats)
{
   wxLogMessage(
      "Transfer stats: %f Kb transferred, %f secs",
      stats.BytesTransferred / 1024.0,
      std::chrono::duration<float>(stats.TransferDuration).count());
}

} // namespace

AudacityProject* OpenProjectFromCloud(
   AudacityProject* potentialTarget, std::string_view projectId,
   std::string_view snapshotId, CloudSyncService::SyncMode mode)
{
   ASSERT_MAIN_THREAD();

   auto authResult = PerformBlockingAuth(potentialTarget);

   if (authResult.Result != AuthResult::Status::Authorised)
   {
      LinkFailedDialog dialog { potentialTarget != nullptr ?
                                   &ProjectWindow::Get(*potentialTarget) :
                                   nullptr };
      dialog.ShowModal();
      return nullptr;
   }

   auto progressDialog = MakeProgress();

   auto future = CloudSyncService::Get().OpenFromCloud(
      std::string(projectId), std::string(snapshotId), mode,
      MakePoller(*progressDialog));

   auto result = GetResult(future);
   LogTransferStats(result.Stats);

   progressDialog.reset();

   const auto conflictResolution =
      GetConfilctResolution(potentialTarget, result);

   if (conflictResolution == ConflictResolution::Stop)
      return nullptr;

   if (conflictResolution == ConflictResolution::Remote)
   {
      return OpenProjectFromCloud(
         potentialTarget, projectId, snapshotId,
         CloudSyncService::SyncMode::ForceOverwrite);
   }

   if (HandleFailure(result))
      return nullptr;

   auto project = ProjectManager::OpenProject(
      GetPotentialTarget(), audacity::ToWXString(result.ProjectPath), true,
      false);

   if (project != nullptr && mode == CloudSyncService::SyncMode::ForceNew)
      ProjectFileIO::Get(*project).MarkTemporary();

   return project;
}

AudacityProject* OpenProjectFromCloud(
   AudacityProject* potentialTarget, std::string_view projectId,
   std::string_view snapshotId, bool forceNew)
{
   return OpenProjectFromCloud(
      potentialTarget, projectId, snapshotId,
      forceNew ? CloudSyncService::SyncMode::ForceNew :
                 CloudSyncService::SyncMode::Normal);
}

bool SyncCloudProject(
   AudacityProject& project, std::string_view path, bool force)
{
   ASSERT_MAIN_THREAD();

   if (!CloudSyncService::Get().IsCloudProject(std::string(path)))
      return true;

   auto authResult = PerformBlockingAuth(&project);

   if (authResult.Result != AuthResult::Status::Authorised)
   {
      LinkFailedDialog dialog { &ProjectWindow::Get(project) };
      dialog.ShowModal();
      return false;
   }

   auto progressDialog = MakeProgress();

   auto future = CloudSyncService::Get().SyncProject(
      project, std::string(path), force, MakePoller(*progressDialog));

   auto result = GetResult(future);
   LogTransferStats(result.Stats);

   progressDialog.reset();

   const auto conflictResolution = GetConfilctResolution(&project, result);

   if (conflictResolution == ConflictResolution::Stop)
      return false;

   if (conflictResolution == ConflictResolution::Remote)
      return SyncCloudProject(project, path, true);

   if (HandleFailure(result))
      return false;

   return true;
}

bool HandleProjectLink(std::string_view uri)
{
   ASSERT_MAIN_THREAD();

   const auto parsedUri = ParseUri(uri);

   if (parsedUri.Scheme != "audacity" || parsedUri.Host != "open")
      return false;

   const auto queryParameters = ParseUriQuery(parsedUri.Query);

   if (queryParameters.empty())
      return false;

   const auto projectId = queryParameters.find("projectId");

   if (projectId == queryParameters.end())
      return false;

   const auto snapshotId = queryParameters.find("snapshotId");

   const auto forceNew = queryParameters.find("new") != queryParameters.end();

   OpenProjectFromCloud(
      GetPotentialTarget(), projectId->second,
      snapshotId != queryParameters.end() ? snapshotId->second :
                                            std::string_view {},
      forceNew);

   return true;
}

bool HandleMixdownLink(std::string_view uri)
{
   ASSERT_MAIN_THREAD();

   const auto parsedUri = ParseUri(uri);

   if (parsedUri.Scheme != "audacity" || parsedUri.Host != "generate-audio")
      return false;

   const auto queryParameters = ParseUriQuery(parsedUri.Query);

   if (queryParameters.empty())
      return false;

   const auto projectId = queryParameters.find("projectId");

   if (projectId == queryParameters.end())
      return false;

   const auto begin = AllProjects {}.begin(), end = AllProjects {}.end();
   auto iter = std::find_if(
      begin, end,
      [&](const AllProjects::value_type& ptr)
      {
         return projectId->second ==
                ProjectCloudExtension::Get(*ptr).GetCloudProjectId();
      });

   const bool hasOpenProject = iter != end;

   const auto project = hasOpenProject ?
                           iter->get() :
                           OpenProjectFromCloud(
                              GetPotentialTarget(), projectId->second,
                              std::string_view {}, false);

   if (project == nullptr)
      return false;

   UploadMixdown(
      *project,
      [hasOpenProject](AudacityProject& project, MixdownState state)
      {
         if (!hasOpenProject)
            ProjectWindow::Get(project).Close(true);
      });

   return true;
}

void UploadMixdown(
   AudacityProject& project,
   std::function<void(AudacityProject&, MixdownState)> onComplete)
{
   SaveToCloud(
      project, UploadMode::Normal,
      [&project, onComplete = std::move(onComplete)](const auto& response)
      {
         auto cancellationContext = concurrency::CancellationContext::Create();

         auto progressDialog = BasicUI::MakeProgress(
            XO("Save to audio.com"), XO("Generating audio preview..."),
            BasicUI::ProgressShowCancel);

         auto mixdownUploader = MixdownUploader::Upload(
            cancellationContext, GetServiceConfig(), project,
            [progressDialog = progressDialog.get(),
             cancellationContext](auto progress)
            {
               if (
                  progressDialog->Poll(
                     static_cast<unsigned>(progress * 10000), 10000) !=
                  BasicUI::ProgressResult::Success)
                  cancellationContext->Cancel();
            });

         mixdownUploader->SetUrls(response.SyncState.MixdownUrls);

         BasicUI::CallAfter(
            [&project,
             progressDialog = std::shared_ptr { std::move(progressDialog) },
             mixdownUploader, cancellationContext, onComplete]() mutable
            {
               auto& projectCloudExtension =
                  ProjectCloudExtension::Get(project);

               auto subscription = projectCloudExtension.SubscribeStatusChanged(
                  [progressDialog = progressDialog.get(), mixdownUploader,
                   cancellationContext](
                     const CloudStatusChangedMessage& message)
                  {
                     if (message.Status != ProjectSyncStatus::Failed)
                        return;

                     cancellationContext->Cancel();
                  },
                  true);

               auto future = mixdownUploader->GetResultFuture();

               while (future.wait_for(std::chrono::milliseconds(50)) !=
                      std::future_status::ready)
                  BasicUI::Yield();

               auto result = future.get();

               progressDialog.reset();

               if (onComplete)
                  onComplete(project, result.State);
            });
      });
}

void ReopenProject(AudacityProject& project)
{
   auto& projectCloudExtension = ProjectCloudExtension::Get(project);

   if (!projectCloudExtension.IsCloudProject())
      return;

   BasicUI::CallAfter(
      [&project,
       projectId = std::string(projectCloudExtension.GetCloudProjectId())]
      {
         auto newProject = ProjectManager::New();
         ProjectWindow::Get(project).Close(true);
         OpenProjectFromCloud(
            newProject, projectId, {},
            CloudSyncService::SyncMode::ForceOverwrite);
      });
}
} // namespace audacity::cloud::audiocom::sync
