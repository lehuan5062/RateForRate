# Unreal MCP toolsets (UE 5.8.4, captured from the live server)

Captured with `Tools/mcp_dump.ps1`. Meta-tools: `list_toolsets`, `describe_toolset {toolset_name}`, `call_tool {toolset_name, tool_name, arguments}`.
Call with `node Tools/mcp_call.mjs call <toolset_name> <tool_name> '<json>'` while `Tools/mcp_editor.ps1 start` is running.

## AutomationTestToolset.AutomationTestToolset
Automation test discovery and execution toolset.

- `StopTests()`: Stop all currently running tests. Requires DiscoverTests() to have completed.
- `RunTestsByFilter(filterExpression)`: Run automation tests selected by a filter expression. Requires DiscoverTests()
- `RunTests(testNames)`: Run a set of automation tests by name. Requires DiscoverTests() to have completed.
- `ListTests(nameFilter, tagFilter)`: List available automation tests. Requires DiscoverTests() to have completed.
- `GetTestStatus()`: Get a lightweight status snapshot of the automation controller.
- `GetTestResults()`: Get detailed results for the current or most recent test run.
- `DiscoverTests()`: Initialize automation worker discovery and load the test list.

## ConfigSettingsToolset.ConfigSettingsToolset
Tools for listing, inspecting, and editing Config Settings sections

- `SetSectionProperties(containerName, categoryName, sectionName, propertiesJson)`: Sets one or more properties on a settings section from a JSON object and saves.
- `SaveSection(containerName, categoryName, sectionName)`: Saves the settings in a section.
- `ResetSectionToDefaults(containerName, categoryName, sectionName)`: Resets the settings in a section to their default values.
- `ListSections(containerName, categoryName)`: Lists the names of all sections within a settings category, sorted alphabetically.
- `ListContainers()`: Lists the names of all known settings containers, sorted alphabetically.
- `ListCategories(containerName)`: Lists the names of all categories within a settings container, sorted alphabetically.
- `GetSectionSchema(containerName, categoryName, sectionName)`: Returns a JSON Schema describing the user-visible properties of a settings section.
- `GetSectionPropertyValues(containerName, categoryName, sectionName, propertyNames)`: Returns the current values of the specified properties as a JSON object.

## DataRegistryToolset.DataRegistryTools
Provides tools for querying and inspecting Data Registries.

- `ListRuntimeSources(registryName)`: Returns the runtime sources for a Data Registry.
- `ListRegistries()`: Returns the names of all registered Data Registries.
- `ListItems(registryName)`: Returns all item names in a Data Registry.
- `ListDataSources(registryName)`: Returns the editor-defined sources configured on a Data Registry.
- `GetSchema(registryName)`: Returns the item struct schema as JSON.
- `GetRegistryInfo(registryName)`: Returns detailed information about a specific registry.
- `GetItems(registryName, itemNames)`: Returns cached item data.

## EditorToolset.EditorAppToolset
Tools for querying and modifying Unreal Editor state: console variables, asset imaging,

- `WorldPosToScreenCoords(position)`: Converts a world-space position into normalized screen space based on the editor viewport camera.
- `StopPIE()`: Stops the currently running play session (PIE or Simulate).
- `StartPIE(options)`: Starts a Play-In-Editor or Simulate-In-Editor session using the current level.
- `SetContentBrowserPath(path)`: Navigates the active content browser to the specified folder path.
- `SetCameraTransform(transform)`: Sets the position and rotation of the level viewport camera.
- `SelectAssets(assetPaths)`: Selects the specified assets in the content browser.
- `SelectActors(actors)`: Selects the specified actors in the current scene.
- `SearchCVars(name)`: Finds all console variables that contain a given name.
- `ScreenCoordsToWorld(coords)`: Finds the world position of the nearest solid object at a given set of normalized view space coords.
- `OpenEditorForAsset(assetPath)`: Opens an asset editor for the specified asset.
- `IsPIERunning()`: Returns whether a Play In Editor session is currently running.
- `GetVisibleActors()`: Returns all actors in the current level whose bounds intersect the viewport frustum.
- `GetSelectedAssets()`: Gets the list of assets selected in the content browser.
- `GetSelectedActors()`: Gets the currently selected actors in the level editor.
- `GetOpenAssets()`: Gets the list of assets currently open in asset editors.
- `GetContentBrowserPath()`: Gets the current path of the active content browser.
- `GetCameraTransform()`: Returns the position and rotation of the level viewport camera.
- `FocusOnActors(actors)`: Repositions the level editor camera to focus on the specified actors.
- `CaptureViewport()`: Captures the level viewport with optional annotations.
- `CaptureEditorImage()`: Captures an image of the entire editor application as the user sees it.
- `CaptureAssetImage(assetPath)`: Renders a thumbnail for the specified asset (e.g. static meshes, skeletal meshes,

## EditorToolset.LogsToolset
Provides tools for reading the Unreal Engine output log and controlling

- `SetVerbosity(verbosity)`: Sets the verbosity level for a log category.
- `GetVerbosity()`: Returns the current verbosity level for a log category.
- `GetLogEntries(pattern)`: Returns log entries from the current session's log file.
- `GetLogCategories(filter)`: Returns a sorted list of registered log categories.

## GameFeaturesToolset.GameFeaturesToolset
Provides tools for listing, activating, and deactivating Game Feature Plugins.

- `RequestDeactivateGameFeature(pluginName)`: Requests deactivation of a Game Feature Plugin.
- `RequestActivateGameFeature(pluginName)`: Requests activation of a Game Feature Plugin.
- `ListEnabledGameFeaturePlugins()`: Lists all enabled Game Feature Plugins  sorted by name. Enabled plugins are the only plugins
- `ListDiscoveredGameFeaturePlugins()`: Lists all discovered Game Feature Plugins sorted by name. This includes enabled and disabled
- `IsGameFeaturePlugin(pluginName)`: Return whether or not a plugin is a Game Feature Plugin. Will error if no plugin of this
- `IsGameFeatureActive(pluginName)`: Checks whether a Game Feature Plugin is active. Raises an error if the subsystem is unavailable
- `GetGameFeatureState(pluginName)`: Gets the current state of a Game Feature Plugin.

## PluginToolset.PluginToolset
Tools for creating, editing, enabling, and querying Unreal plugins

- `ValidateNewPluginNameAndLocation(pluginName, relativePluginLocation, bPlaceInEngine, templateInfo)`: Validates that PluginName and RelativePluginLocation are acceptable for a new plugin.
- `UpdatePluginDescriptor(pluginName, newDescriptor)`: Updates a plugin's descriptor fields and writes them to its .uplugin file.
- `SetPluginEnabled(pluginName, bEnabled)`: Enables or disables a plugin in the project config. The change takes effect on the next editor restart.
- `RemovePluginDependency(pluginName, dependencyName)`: Removes a dependency entry from a plugin's Plugins array in its .uplugin file.
- `ListEnabledPlugins()`: Lists the names of all enabled plugins, sorted alphabetically.
- `ListDiscoveredPlugins()`: Lists the names of all discovered plugins (enabled and disabled), sorted alphabetically.
- `IsPluginModificationAllowed()`: Checks whether the editor settings permit modifying plugins from the plugin browser.
- `IsPluginCreationAllowed()`: Checks whether the editor settings permit plugin creation from the plugin browser.
- `IsEnabled(pluginName)`: Checks whether a discovered plugin is currently enabled.
- `GetPluginTemplateDescriptions()`: Returns the list of available plugin templates. Pass one of the results to CreatePlugin
- `GetPluginInfo(pluginName)`: Gets metadata for a discovered plugin, including description, version, base directory,
- `GetPluginForAsset(assetPath)`: Returns the name of the enabled plugin whose content mount point contains the given asset path.
- `GetPluginDescriptor(pluginName)`: Gets the editable descriptor fields for a discovered plugin.
- `GetPluginDependents(pluginName)`: Returns the names of all discovered plugins that declare a dependency on the given plugin.
- `GetPluginDependencies(pluginName)`: Returns the dependency entries from a plugin's Plugins array in its .uplugin file.
- `CreatePlugin(pluginName, relativePluginLocation, bPlaceInEngine, templateInfo, description)`: Creates a new plugin from a template and loads it into the editor.
- `AddPluginDependency(pluginName, dependencyName, bOptional, bEnabled)`: Adds a dependency entry to a plugin's Plugins array in its .uplugin file.

## SlateInspectorToolset.SlateInspectorToolset
Playwright-style Slate UI automation toolset.

- `Windows()`: List, select, or close top-level Slate editor windows.
- `WaitFor(text, textGone)`: Check if text is present or absent in the Slate widget tree.
- `Unobserve(identifier)`: Remove an observer by its identifier.
- `Type(ref, text)`: Type text into a Slate text input widget. Focuses the widget first,
- `Snapshot(ref)`: Capture a Slate UI accessibility snapshot.  Use this to read the current
- `SelectOption(ref, value)`: Select an option in a Slate combobox by its text label.
- `Screenshot(ref)`: Screenshot a Slate widget or the active editor window. Prefer this over
- `PressKey(key)`: Press and release a keyboard key on the currently focused Slate widget.
- `Observe(ref)`: Register an observer on a widget subtree so its refs are continuously
- `ListObservers()`: List all active observers as a JSON array for debugging.
- `Hover(ref)`: Hover over a Slate widget, triggering any hover state or tooltip.
- `FillForm(fields)`: Fill multiple Slate form fields at once.
- `Drag(startRef, endRef)`: Drag from one Slate widget to another (mouse down, move, release).
- `Click(ref)`: Click a Slate widget identified by its ref.

## editor_toolset.toolsets.actor.ActorTools
Provides tools for inspecting and modifying actors, including

- `remove_component(component)`: Removes a component from an actor instance or blueprint.
- `add_component(owner, component_type, name)`: Adds a component to an actor instance or blueprint.
- `get_components(actor)`: Returns the components that an actor contains.
- `get_actor_bounds(actor)`: Returns the bounding box of an actor.
- `set_parent_component(component)`: Sets the parent for the specified scene component.
- `get_parent_component(component)`: Returns the parent component that this component is attached to, if any.
- `get_component_actor(component)`: Returns the actor that owns the specified component.
- `get_root_component(actor)`: Returns the root component of an actor, if any.
- `look_at(actor, target)`: Rotates an actor so its forward vector points at a world-space position.
- `set_actor_transform(actor, xform)`: Updates the position, rotation, and/or scale of an actor.
- `get_actor_transform(actor)`: Returns the position, rotation, and scale of an actor.
- `remove_tag(actor, tag)`: Removes a tag from an actor.
- `add_tag(actor, tag)`: Adds a tag to an actor.
- `has_tag(actor, tag)`: Returns whether an actor has a specific tag.
- `get_tags(actor)`: Returns the list of tags on an actor.
- `set_label(actor, label)`: Sets the human-friendly name of the actor.
- `get_label(actor)`: Returns the actor's human friendly name as it appears in the editor.

## editor_toolset.toolsets.asset.AssetTools
Provides tools for interacting with assets in the Unreal project and files on disk.

- `write_file(file_path, content)`: Writes text content to a file on disk.
- `read_file(file_path)`: Reads a text file from disk and returns its contents.
- `get_plugin_content_paths()`: Returns the root content paths for plugins that have content.
- `get_dependencies(asset_path)`: Lists assets that the specified asset depends on.
- `get_referencers(asset_path)`: Lists assets that reference the specified asset.
- `is_checked_out(asset_path)`: Checks whether an asset is checked out by the current user.
- `can_edit_asset(asset_path)`: Checks whether an asset can be edited.
- `is_dirty(asset_path)`: Checks whether an asset has unsaved changes.
- `save_assets(asset_paths)`: Saves assets to disk.
- `load_asset(asset_path)`: Loads an asset from the project.
- `update_metadata_tags(asset_path)`: Sets or removes metadata tags on an asset.
- `get_metadata_tags(asset_path)`: Gets the metadata tags for an asset.
- `get_asset_class(asset_path)`: Gets the class of an asset.
- `get_asset_tags(asset_path)`: Gets the asset registry tags for an asset.
- `find_assets(folder_path, name)`: Searches the project for assets that match specific criteria.
- `delete(path)`: Deletes an asset or folder.
- `move(path, new_path)`: Moves or renames an asset or folder.
- `duplicate(path, new_path)`: Makes a copy of a folder or asset.
- `exists(path)`: Determines if a folder or asset exists.
- `list_folders(root_path)`: Lists the folders contained within a folder.
- `create_folder(path)`: Creates a folder at the specified path.

## editor_toolset.toolsets.curve_table.CurveTableTools
Provides tools for creating and editing CurveTable assets.

- `get_keys(curve_table, row_name)`: Returns all keys for a row.
- `set_keys(curve_table, row_name, keys)`: Replaces all keys in a row with the provided list.
- `add_key(curve_table, row_name, key)`: Adds a key to a row.
- `rename_row(curve_table, row_name, new_row_name)`: Renames a row in the curve table.
- `remove_row(curve_table, row_name)`: Removes a row from the curve table.
- `add_row(curve_table, row_name)`: Adds a new row to the curve table with an optional default value.
- `list_rows(curve_table)`: Lists the names of all rows in the curve table.
- `create(folder_path, asset_name)`: Creates a new CurveTable asset.
- `import_file(folder_path, asset_name, source_file, interp_mode)`: Imports a file from disk as a CurveTable asset.

## editor_toolset.toolsets.data_asset.DataAssetTools
Provides tools for working with Data Assets.

- `create(folder_path, asset_name, asset_type)`: Creates a new DataAsset asset in the project.

## editor_toolset.toolsets.data_table.DataTableTools
Provides tools for creating and editing DataTable assets.

- `set_rows(data_table, values)`: Sets column values for one or more rows.
- `get_rows(data_table, row_names)`: Returns the column values for one or more rows as a JSON string.
- `rename_rows(data_table, renames)`: Renames one or more rows in the data table.
- `remove_rows(data_table, row_names)`: Removes rows from the data table.
- `add_rows(data_table, row_names)`: Adds new rows with default values to the data table.
- `list_rows(data_table)`: Lists the names of all rows in the data table.
- `get_schema(data_table)`: Returns the column schema of the data table as a JSON string.
- `create(folder_path, asset_name, schema)`: Creates a new DataTable asset with the specified column schema.
- `import_file(folder_path, asset_name, source_file, schema)`: Imports a file from disk as a DataTable asset.
- `search_row_structs()`: Finds structs that can be used as a DataTable schema.

## editor_toolset.toolsets.material.MaterialTools
Provides tools for creating and editing Material and MaterialFunction assets.

- `get_referencing_materials(material_function)`: Returns asset data for all Materials that reference this MaterialFunction.
- `recompile(material_or_function)`: Recompiles a Material or MaterialFunction after edits.
- `delete_unused_expressions(material)`: Deletes all expression nodes not connected to any material output.
- `disconnect_from_output(material, material_property)`: Disconnects the expression currently connected to a material output property.
- `connect_to_output(expression, output_name, material_property)`: Connects an expression node's output to one of the material's output properties.
- `get_property_input(material, material_property)`: Returns the expression and output pin feeding a material output property.
- `get_expression_inputs(material_or_function, expression)`: Returns the current wiring of each input pin on a material expression.
- `disconnect_expressions(to_expression, to_input_name)`: Disconnects the input pin of an expression node, removing whatever is connected to it.
- `connect_expressions(from_expression, from_output_name, to_expression, to_input_name)`: Connects an expression node's output pin to another expression node's input pin.
- `get_expression_output_names(expression)`: Returns the names of all output pins on a material expression node.
- `get_expression_input_names(expression)`: Returns the names of all input pins on a material expression node.
- `delete_parameter_group(material_or_function, group_name)`: Removes a parameter group, ungrouping all parameters that belong to it.
- `rename_parameter_group(material_or_function, old_name, new_name)`: Renames a parameter group across all parameter expressions in a Material or
- `list_parameter_groups(material_or_function)`: Returns the unique parameter group names defined in a Material or MaterialFunction.
- `layout_expressions(material_or_function)`: Automatically arranges all expression nodes in a Material or MaterialFunction graph.
- `get_expressions(material_or_function)`: Returns all expression nodes in a Material or MaterialFunction graph.
- `delete_expression(material_or_function, expression)`: Removes an expression node from a Material or MaterialFunction graph.
- `add_expression(material_or_function, expression_class)`: Adds a new expression node to a Material or MaterialFunction graph.
- `list_expression_classes(material_or_function, search)`: Returns MaterialExpression subclasses valid for the given context.
- `create_parameter_collection(folder_path, asset_name)`: Creates a new empty MaterialParameterCollection (MPC) asset.
- `create_function(folder_path, asset_name)`: Creates a new empty MaterialFunction asset.
- `create_material(folder_path, asset_name)`: Creates a new empty Material asset.

## editor_toolset.toolsets.object.ObjectTools
Provides tools for inspecting and modifying the properties of

- `reset_properties(instance, properties)`: Resets one or more properties on an object to their default values,
- `set_properties(instance, values)`: Sets the values of properties on an object.
- `get_properties(instance, properties)`: Returns the values of one or more properties on an object.
- `list_properties(instance)`: Returns a list of properties that are on the specified object.
- `get_class(instance)`: Returns the class of an Unreal object.
- `search_subclasses(base_class, class_name)`: Finds all subclasses of a given class.

## editor_toolset.toolsets.programmatic.ProgrammaticToolset
Batches calls to other registered tools through a small sandboxed Python

- `execute_tool_script(script)`: Execute a Python script against the toolset APIs.
- `get_execution_environment()`: Get details about execution environment.

## editor_toolset.toolsets.scene.SceneTools
Provides tools for working with the currently loaded level, including

- `save_actor(actor)`: Saves the actor to disk.
- `is_checked_out(actor)`: Checks whether an actor is checked out by the current user.
- `can_edit(actor)`: Checks whether an actor can be edited.
- `commit_level_instance(level_instance)`: Saves or discards edits to a level instance and exits edit mode.
- `edit_level_instance(level_instance)`: Opens a level instance for editing.
- `create_level_instance(level_path, name, xform)`: Creates a Level Instance actor in the scene referencing an existing level asset.
- `merge_actors(actors, output_path, name)`: Merges multiple StaticMesh actors into a single mesh asset and actor.
- `trace_world(start, end)`: Traces a line through the world and returns the distance to the first hit.
- `delete_folder(folder_path)`: Deletes a folder from the outliner.
- `rename_folder(old_path, new_path)`: Renames a folder in the outliner.
- `set_actor_folder(actor, folder_path)`: Assigns an actor to the specified folder in the outliner.
- `get_actors_in_folder(folder_path)`: Returns the actors in the specified outliner folder.
- `get_folders()`: Returns all folder paths currently in use in the outliner.
- `remove_from_scene(actor)`: Deletes an actor from the scene.
- `add_to_scene_from_asset(asset_path, name, xform)`: Creates a new actor in the scene from an asset.
- `add_to_scene_from_class(actor_type, name, xform)`: Creates a new instance of the specified object at the specified transform.
- `find_actors(name, tag, collision_channels)`: Searches the scene for actors that match specific criteria.
- `get_collision_channels()`: Returns all available collision channels for use with find_actors.
- `get_current_level()`: Returns the path to the current level asset.
- `load_level(level_path)`: Loads a level in the editor.

## state_tree_toolset.toolsets.state_tree.StateTreeTools
Inspect StateTree (ST) assets.

- `get_node_description(state_tree, node)`: Returns a human-readable description for a node.
- `get_evaluators(state_tree)`: Returns global evaluators.
- `get_global_tasks(state_tree)`: Returns global tasks that run across all states.
- `get_transitions(state)`: Returns transitions on a state.
- `get_enter_conditions(state)`: Returns enter conditions on a state.
- `get_tasks(state)`: Returns tasks on a state.
- `get_children(state)`: Returns child states of a state.
- `get_root_states(state_tree)`: Returns top-level states of a StateTree.
- `get_editor_data(state_tree)`: Returns the editor data for a StateTree asset.
