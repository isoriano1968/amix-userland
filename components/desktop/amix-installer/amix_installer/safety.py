"""Checks before anything is written (used by the backend from version 0.3).

Read-only: nothing here writes.
"""
import os


class Refused(Exception):
    pass


def same_file(a, b):
    try:
        return os.path.samefile(a, b)
    except OSError:
        return os.path.realpath(a) == os.path.realpath(b)


def check_image_target(source, target):
    """A new image file must not be the source, nor overwrite it through a link."""
    if not target:
        raise Refused("no target chosen")
    if source and (same_file(source, target) or
                   os.path.realpath(source) == os.path.realpath(target)):
        raise Refused("the target is the source image; the source is never written")
    if os.path.exists(target):
        raise Refused("%s exists; the installer writes new files only" % target)
    parent = os.path.dirname(os.path.abspath(target)) or "."
    if not os.path.isdir(parent):
        raise Refused("folder %s does not exist" % parent)


def check_output_folder(source, folder):
    if not folder or not os.path.isdir(folder):
        raise Refused("choose an existing folder for the new HDF files")
    if not os.access(folder, os.W_OK):
        raise Refused("folder %s is not writable" % folder)
