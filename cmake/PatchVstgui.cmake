# Patches VSTGUI (inside the fetched VST3 SDK) so a drop that carries a file and some text hands
# over the file. A clip dragged out of REAPER or Ableton Live carries both (its name as text, its
# audio file as a file URL / CF_HDROP), and VSTGUI as released takes the text on macOS and Windows,
# so the plug-ins never saw the file. Every edit is checked: already patched is left alone, and text
# that is no longer there (a newer SDK) stops the configure, so the patch is looked at again.

# One replacement in `content` (the caller's variable); the old text must be there.
function(_pk_replace old new)
    string(FIND "${content}" "${old}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "pluginkit: VSTGUI patch: the text to replace is not in ${_pk_file} (a different SDK version?)")
    endif()
    string(REPLACE "${old}" "${new}" content "${content}")
    set(content "${content}" PARENT_SCOPE)
endfunction()

function(pk_patch_vstgui sdk)
    set(vg "${sdk}/vstgui4/vstgui/lib/platform")

    set(marker "pluginkit: files before text")

    # macOS: take an item's file URL before its string (resolving a file reference URL to its path)
    set(_pk_file "${vg}/mac/macclipboard.mm")
    file(READ "${_pk_file}" content)
    string(FIND "${content}" "${marker}" patched)
    if(patched EQUAL -1)
        _pk_replace("@[NSPasteboardTypeString, NSPasteboardTypeFileURL, NSPasteboardTypeColor];"
            "@[NSPasteboardTypeFileURL, NSPasteboardTypeString, NSPasteboardTypeColor]; // pluginkit: files before text")
        _pk_replace("std::string pathStr = url.path.UTF8String;"
            "NSURL* fileUrl = url.filePathURL ? url.filePathURL : url;\n\t\t\t\tstd::string pathStr = fileUrl.path ? fileUrl.path.UTF8String : \"\";")
        file(WRITE "${_pk_file}" "${content}")
        message(STATUS "pluginkit: patched ${_pk_file} (dropped files before text)")
    endif()

    # Windows: CF_HDROP before CF_UNICODETEXT
    set(_pk_file "${vg}/win32/win32datapackage.cpp")
    file(READ "${_pk_file}" content)
    string(FIND "${content}" "${marker}" patched)
    if(patched EQUAL -1)
        _pk_replace("	if (!platformDataObject)
		return;

	STGMEDIUM medium = {};"
        "	if (!platformDataObject)
		return;

	// pluginkit: files before text (a clip dragged out of a DAW carries both)
	if (platformDataObject->QueryGetData (&formatHDrop) == S_OK)
	{
		STGMEDIUM files = {};
		if (platformDataObject->GetData (&formatHDrop, &files) == S_OK)
		{
			const uint32_t count = DragQueryFile ((HDROP)files.hGlobal, 0xFFFFFFFFL, nullptr, 0);
			TCHAR fileDropped[1024];
			for (uint32_t index = 0; index < count; index++)
			{
				if (DragQueryFile ((HDROP)files.hGlobal, index, fileDropped, sizeof (fileDropped) / 2))
				{
					checkResolveLink (fileDropped, fileDropped);
					UTF8StringHelper path (fileDropped);
					strings.emplace_back (path);
				}
			}
			ReleaseStgMedium (&files);
			if (!strings.empty ())
			{
				nbItems = static_cast<uint32_t> (strings.size ());
				stringsAreFiles = true;
				return;
			}
		}
	}

	STGMEDIUM medium = {};")
        file(WRITE "${_pk_file}" "${content}")
        message(STATUS "pluginkit: patched ${_pk_file} (dropped files before text)")
    endif()
endfunction()
