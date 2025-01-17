// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
import 'dart:async';

import 'package:flutter/material.dart';

import 'menu_channel.dart';
import 'native_menu_item.dart';

/// Sets the application menu for the app based on the [menuSpec].
///
/// Adjacent [NativeMenuDivider]s will be coalesced, leading and/or trailing
/// [NativeMenuDivider]s will be removed.
Future<Null> setApplicationMenu(List<NativeSubmenu> menuSpec) async {
  await MenuChannel.instance.setMenu(menuSpec);
}

Future<Null> hookApplicationMenu({
  required int id, 
  String? label,
  VoidCallback? onSelected,
}) async {
  await MenuChannel.instance.hookMenu(
    id: id, 
    label: label,
    onSelected: onSelected,
  );
}