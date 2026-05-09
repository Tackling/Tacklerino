// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT
#include "widgets/ChatterListWidget.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchAccount.hpp"  // IWYU pragma: keep
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Theme.hpp"
#include "util/Helpers.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
namespace chatterino {
namespace {
QString formatVIPListError(HelixListVIPsError error, const QString &message)
{
    using Error = HelixListVIPsError;
    QString errorMessage = QString("Failed to list VIPs - ");
    switch (error)
    {
        case Error::Forwarded: {
            errorMessage += message;
        }
        break;
        case Error::Ratelimited: {
            errorMessage += "You are being ratelimited by Twitch. Try "
                            "again in a few seconds.";
        }
        break;
        case Error::UserMissingScope: {
            errorMessage += "Missing required scope. "
                            "Re-login with your "
                            "account and try again.";
        }
        break;
        case Error::UserNotAuthorized: {
            errorMessage += "You don't have permission to "
                            "perform that action.";
        }
        break;
        case Error::UserNotBroadcaster: {
            errorMessage +=
                "Due to Twitch restrictions, "
                "this command can only be used by the broadcaster. "
                "To see the list of VIPs you must use the Twitch website.";
        }
        break;
        case Error::Unknown: {
            errorMessage += "An unknown error has occurred.";
        }
        break;
    }
    return errorMessage;
}
QString formatModsError(HelixGetModeratorsError error, const QString &message)
{
    using Error = HelixGetModeratorsError;
    QString errorMessage = QString("Failed to get moderators: ");
    switch (error)
    {
        case Error::Forwarded: {
            errorMessage += message;
        }
        break;
        case Error::UserMissingScope: {
            errorMessage += "Missing required scope. "
                            "Re-login with your "
                            "account and try again.";
        }
        break;
        case Error::UserNotAuthorized: {
            errorMessage +=
                "Due to Twitch restrictions, "
                "this command can only be used by the broadcaster. "
                "To see the list of mods you must use the Twitch website.";
        }
        break;
        case Error::Unknown: {
            errorMessage += "An unknown error has occurred.";
        }
        break;
    }
    return errorMessage;
}
}  // namespace
ChatterListWidget::ChatterListWidget(const TwitchChannel *twitchChannel,
                                     QWidget *parent)
    : BaseWindow({}, parent)
{
    assert(twitchChannel != nullptr);
    this->setWindowTitle("Chatter List - " + twitchChannel->getName());
    this->setAttribute(Qt::WA_DeleteOnClose);
    auto *dockVbox = new QVBoxLayout();
    auto *searchBar = new QLineEdit(this);
    auto *chattersList = new QListWidget(this);
    auto *resultList = new QListWidget(this);
    auto *loadingLabel = new QLabel("Loading...", this);
    searchBar->setPlaceholderText("Search User...");
    auto formatListItemText = [](const QString &text) {
        auto *item = new QListWidgetItem();
        item->setText(text);
        item->setFont(
            getApp()->getFonts()->getFont(FontStyle::ChatMedium, 1.0));
        return item;
    };
    auto addLabel = [this, formatListItemText,
                     chattersList](const QString &label) {
        auto *formattedLabel = formatListItemText(label);
        formattedLabel->setFlags(Qt::NoItemFlags);
        formattedLabel->setForeground(this->theme->accent);
        chattersList->addItem(formattedLabel);
    };
    auto addUserList = [=](const QStringList &users, QString label) {
        if (users.isEmpty())
        {
            return;
        }
        addLabel(QString("%1 (%2)").arg(label, localizeNumbers(users.size())));
        for (const auto &user : users)
        {
            chattersList->addItem(formatListItemText(user));
        }
        chattersList->addItem(new QListWidgetItem());
    };
    auto performListSearch = [=]() {
        auto query = searchBar->text();
        if (query.isEmpty())
        {
            resultList->hide();
            chattersList->show();
            return;
        }
        auto results = chattersList->findItems(query, Qt::MatchContains);
        chattersList->hide();
        resultList->clear();
        for (auto &item : results)
        {
            if (!item->text().contains("("))
            {
                resultList->addItem(formatListItemText(item->text()));
            }
        }
        resultList->show();
    };

    // Fetch chatters from tackling.cc API
    const QString url =
        QStringLiteral(
            "https://api.tackling.cc/twitch/Chatters?login=%1&limit=20000")
            .arg(twitchChannel->getName());

    NetworkRequest(url)
        .caller(this)
        .onSuccess([=](auto result) {
            const auto obj = result.parseJson();

            auto getList = [&](const QString &key) {
                QStringList list;
                for (const auto &v : obj.value(key).toArray())
                {
                    list << v.toString().toLower();
                }
                return list;
            };

            const auto broadcasters = getList("broadcasters");
            const auto moderators = getList("moderators");
            const auto vips = getList("vips");
            const auto viewers = getList("viewers");
            const auto chatbots = getList("chatbots");
            const auto staff = getList("staff");

            // Broadcaster
            if (!broadcasters.isEmpty())
            {
                addLabel("Broadcaster");
                for (const auto &u : broadcasters)
                    chattersList->addItem(formatListItemText(u));
                chattersList->addItem(new QListWidgetItem());
            }

            // Staff
            QStringList staffSorted = staff;
            staffSorted.sort();
            addUserList(staffSorted, "Staff");

            // Moderators
            QStringList modsSorted = moderators;
            modsSorted.sort();
            addUserList(modsSorted, "Moderators");

            // VIPs
            QStringList vipsSorted = vips;
            vipsSorted.sort();
            addUserList(vipsSorted, "VIPs");

            // Viewers
            QStringList viewersSorted = viewers;
            viewersSorted.sort();
            addUserList(viewersSorted, "Viewers");

            // Chatbots
            QStringList chatbotsSorted = chatbots;
            chatbotsSorted.sort();
            addUserList(chatbotsSorted, "Chatbots");

            loadingLabel->hide();
            performListSearch();
        })
        .onError([=](auto /*result*/) {
            chattersList->addItem(formatListItemText(
                "Failed to fetch chatters from api.tackling.cc"));
            loadingLabel->hide();
        })
        .execute();

    QObject::connect(searchBar, &QLineEdit::textEdited, this,
                     performListSearch);
    this->setMinimumWidth(300);
    auto listDoubleClick = [this](const QModelIndex &index) {
        const auto itemText = index.data().toString();
        if (!itemText.isEmpty())
        {
            this->userClicked(itemText);
        }
    };
    QObject::connect(chattersList, &QListWidget::doubleClicked, this,
                     listDoubleClick);
    QObject::connect(resultList, &QListWidget::doubleClicked, this,
                     listDoubleClick);
    HotkeyController::HotkeyMap actions{
        {"delete",
         [this](const std::vector<QString> &) -> QString {
             this->close();
             return "";
         }},
        {"accept", nullptr},
        {"reject", nullptr},
        {"scrollPage", nullptr},
        {"openTab", nullptr},
        {"search",
         [searchBar](const std::vector<QString> &) -> QString {
             searchBar->setFocus();
             searchBar->selectAll();
             return "";
         }},
    };
    getApp()->getHotkeys()->shortcutsForCategory(HotkeyCategory::PopupWindow,
                                                 actions, this);
    dockVbox->addWidget(searchBar);
    dockVbox->addWidget(loadingLabel);
    dockVbox->addWidget(chattersList);
    dockVbox->addWidget(resultList);
    resultList->hide();
    this->setStyleSheet(this->theme->splits.input.styleSheet);
    this->setLayout(dockVbox);
}
}  // namespace chatterino
