// Expanded Manage Band preferences on the stock GH2 notebook screen.

#pragma once

#include "ui/ui_classes.h"

#include <string>
#include <vector>

namespace ghogx::ui {

class ConfigDb;
class ScreenManager;

class ManageBandPanel final : public UiObject {
 public:
  ManageBandPanel(ScreenManager* manager, ConfigDb* db);
  DataNode handle_property(Symbol msg, const DataArray& args) override;

 private:
  struct Choice {
    Symbol id;
    std::string label;
    std::string model_path;
    std::string animation_path;
  };

  enum class Stage { Categories, Values };

  void enter();
  void handle_button(Symbol button);
  void move(int direction);
  void confirm();
  void back();
  void refresh();
  void update_preview();
  std::vector<Choice> choices_for_category(int category) const;
  Symbol preference(Symbol key) const;
  void set_preference(Symbol key, Symbol value);
  Symbol category_key(int category) const;
  std::string category_label(int category) const;
  std::string localized_label(std::string value) const;
  std::string character_display_label(Symbol value) const;
  std::string display_label(Symbol value) const;
  std::string bass_display_label(Symbol value) const;
  int choice_index(const std::vector<Choice>& choices, Symbol selected) const;

  ConfigDb* db_ = nullptr;
  Stage stage_ = Stage::Categories;
  int category_ = 0;
  int value_index_ = 0;
  int flow_step_ = 0;
  Symbol pending_parent_;
};

void install_manage_band_screen(ScreenManager& manager, ConfigDb& db);

}  // namespace ghogx::ui
