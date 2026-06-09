#!/usr/bin/python
#
# \file 1_export.py
# \brief Export shape
# \date 2010-09-20-18-35-GMT
# \author Jan Boon (Kaetemi)
# Python port of game data build pipeline.
# Export shape
#
# NeL - MMORPG Framework <https://wiki.ryzom.dev/>
# Copyright (C) 2009-2014  by authors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import time, sys, os, shutil, subprocess, multiprocessing
from concurrent.futures import ProcessPoolExecutor, as_completed
sys.path.append("../../configuration")

if os.path.isfile("log.log"):
	os.remove("log.log")
if os.path.isfile("temp_log.log"):
	os.remove("temp_log.log")
log = open("temp_log.log", "w")
from scripts import *
from buildsite import *
from process import *
from tools import *
from directories import *

printLog(log, "")
printLog(log, "-------")
printLog(log, "--- Export shape")
printLog(log, "-------")
printLog(log, time.strftime("%Y-%m-%d %H:%MGMT", time.gmtime(time.time())))
printLog(log, "")

# Override config values for draft mode
if BuildQuality == 0:
	ShapeExportOptExportLighting = "false"
	ShapeExportOptShadow = "false"
	ShapeExportOptLightingLimit = 0
	ShapeExportOptLumelSize = "0.25"
	ShapeExportOptOversampling = 1


# --- nel-export: parallel .shape → .glb (replaces 3DS Max dependency) ---
#
# Requires nel_export binary built from ryzomcore/nel/tools/3d/nel_export/.
# Uses ProcessPoolExecutor for CPU-parallel conversion.
# Tag files in ShapeTagExportDirectory skip unchanged shapes on incremental builds.

def _nel_export_one(args):
	"""Worker: run nel-export on one .shape file. Returns (srcFile, rc, stderr)."""
	nel_export_bin, src_file, out_file = args
	result = subprocess.run(
		[nel_export_bin, "--shape", src_file, "-o", out_file],
		capture_output=True, text=True
	)
	return src_file, out_file, result.returncode, result.stdout + result.stderr

NelExport = findTool(log, ToolDirectories, NelExportTool, ToolSuffix)
printLog(log, "")

if NelExport != "":
	printLog(log, ">>> Export shape via nel-export (parallel) <<<")

	glbDir = ExportBuildDirectory + "/" + ShapeGlbExportDirectory
	tagDir = ExportBuildDirectory + "/" + ShapeTagExportDirectory
	mkPath(log, glbDir)
	mkPath(log, tagDir)

	# Collect jobs: (binary, srcFile, outFile) for each .shape needing update
	jobs = []
	for srcDir in ShapeSourceDirectories:
		fullSrcDir = DatabaseDirectory + "/" + srcDir
		if not os.path.isdir(fullSrcDir):
			continue
		for root, _dirs, files in os.walk(fullSrcDir):
			for fname in files:
				if not fname.lower().endswith(".shape"):
					continue
				srcFile = os.path.join(root, fname)
				stem = os.path.splitext(fname)[0]
				outFile = os.path.join(glbDir, stem + ".glb")
				tagFile = os.path.join(tagDir, stem + ".shape.glb.tag")
				if not needUpdate(log, srcFile, outFile):
					printLog(log, "SKIP " + srcFile)
					continue
				jobs.append((NelExport, srcFile, outFile))

	if jobs:
		cores = max(1, multiprocessing.cpu_count())
		printLog(log, "Converting " + str(len(jobs)) + " shapes on " + str(cores) + " cores")
		ok = 0
		fail = 0
		with ProcessPoolExecutor(max_workers=cores) as executor:
			futs = {executor.submit(_nel_export_one, j): j for j in jobs}
			for fut in as_completed(futs):
				srcFile, outFile, rc, output = fut.result()
				stem = os.path.splitext(os.path.basename(srcFile))[0]
				tagFile = os.path.join(tagDir, stem + ".shape.glb.tag")
				if rc == 0:
					ok += 1
					# Write tag file so incremental builds skip this file
					with open(tagFile, "w") as tf:
						tf.write(time.strftime("%Y-%m-%d %H:%MGMT", time.gmtime(time.time())) + "\n")
				else:
					fail += 1
					printLog(log, "FAIL " + srcFile)
					for line in output.splitlines():
						line = line.strip()
						if line:
							printLog(log, "  " + line)
		printLog(log, "nel-export: " + str(ok) + " ok, " + str(fail) + " failed.")
	else:
		printLog(log, "nel-export: all shapes up to date.")
	printLog(log, "")
else:
	printLog(log, "nel-export not found — skipping .shape → .glb export")
	printLog(log, "  Build it: cmake --build ryzomcore/build --target nel_export")
	printLog(log, "")


# --- mesh_export (assimp): .blend/.obj/.dae/.gltf/.glb source files ---

MeshExport = findTool(log, ToolDirectories, MeshExportTool, ToolSuffix)
printLog(log, "")

AssimpFormats = [ ".blend", ".obj", ".dae", ".gltf", ".glb" ]

if MeshExport != "":
	printLog(log, ">>> Export shape assimp <<<")
	tagDirectory = ExportBuildDirectory + "/" + ShapeTagExportDirectory
	mkPath(log, tagDirectory)
	outDirWithoutCoarse = ExportBuildDirectory + "/" + ShapeNotOptimizedExportDirectory
	mkPath(log, outDirWithoutCoarse)
	for dir in ShapeSourceDirectories:
		srcDirectory = DatabaseDirectory + "/" + dir
		mkPath(log, srcDirectory)
		for format in AssimpFormats:
			files = findFilesNoSubdir(log, srcDirectory, format)
			for file in files:
				srcFile = srcDirectory + "/" + file
				tagFile = tagDirectory + "/" + file + ".tag"
				if not needUpdate(log, srcFile, tagFile):
					printLog(log, "SKIP " + srcFile)
					continue
				printLog(log, "MESH EXPORT " + srcFile)
				subprocess.call([ MeshExport, "-d", outDirWithoutCoarse, srcFile ])
				tagFile = open(tagFile, "w")
				tagFile.write(time.strftime("%Y-%m-%d %H:%MGMT", time.gmtime(time.time())) + "\n")
				tagFile.close()

# --- 3DS Max (legacy, requires 3DS Max 2010-2012 32-bit) ---
# This path is disabled in the modernized pipeline. nel-export handles .shape
# files directly without 3DS Max. Kept for reference only.

if MaxAvailable:
	# Find tools
	Max = findMax(log, MaxDirectory, MaxExecutable)
	printLog(log, "")

	printLog(log, ">>> Export shape 3dsmax <<<")
	tagDirectory = ExportBuildDirectory + "/" + ShapeTagExportDirectory
	mkPath(log, tagDirectory)
	outDirWithoutCoarse = ExportBuildDirectory + "/" + ShapeNotOptimizedExportDirectory
	mkPath(log, outDirWithoutCoarse)
	outDirWithCoarse = ExportBuildDirectory + "/" + ShapeWithCoarseMeshExportDirectory
	mkPath(log, outDirWithCoarse)
	outDirLightmap = ExportBuildDirectory + "/" + ShapeLightmapNotOptimizedExportDirectory
	mkPath(log, outDirLightmap)
	outDirAnim = ExportBuildDirectory + "/" + ShapeAnimExportDirectory
	mkPath(log, outDirAnim)
	for dir in ShapeSourceDirectories:
		srcDirectory = DatabaseDirectory + "/" + dir
		mkPath(log, srcDirectory)
		if (needUpdateDirByTagLog(log, srcDirectory, ".max", tagDirectory, ".max.tag")):
			scriptSrc = "maxscript/shape_export.ms"
			scriptDst = MaxUserDirectory + "/scripts/shape_export.ms"
			outputLogfile = ScriptDirectory + "/processes/shape/log.log"
			maxRunningTagFile = tagDirectory + "/max_running.tag"
			maxSourceDir = DatabaseDirectory + "/" + dir
			tagList = findFiles(log, tagDirectory, "", ".max.tag")
			tagLen = len(tagList)
			if os.path.isfile(scriptDst):
				os.remove(scriptDst)
			tagDiff = 1
			sSrc = open(scriptSrc, "r")
			sDst = open(scriptDst, "w")
			for line in sSrc:
				newline = line.replace("%OutputLogfile%", outputLogfile)
				newline = newline.replace("%MaxSourceDirectory%", maxSourceDir)
				newline = newline.replace("%TagDirectory%", tagDirectory)
				newline = newline.replace("%OutputDirectoryWithoutCoarseMesh%", outDirWithoutCoarse)
				newline = newline.replace("%OutputDirectoryWithCoarseMesh%", outDirWithCoarse)
				newline = newline.replace("%OutputDirectoryLightmap%", outDirLightmap)
				newline = newline.replace("%OutputDirectoryAnim%", outDirAnim)
				newline = newline.replace("%ShapeExportOptExportLighting%", ShapeExportOptExportLighting)
				newline = newline.replace("%ShapeExportOptShadow%", ShapeExportOptShadow)
				newline = newline.replace("%ShapeExportOptLightingLimit%", str(ShapeExportOptLightingLimit))
				newline = newline.replace("%ShapeExportOptLumelSize%", ShapeExportOptLumelSize)
				newline = newline.replace("%ShapeExportOptOversampling%", str(ShapeExportOptOversampling))
				newline = newline.replace("%ShapeExportOptLightmapLog%", ShapeExportOptLightmapLog)
				sDst.write(newline)
			sSrc.close()
			sDst.close()
			zeroRetryLimit = 3
			while tagDiff > 0:
				mrt = open(maxRunningTagFile, "w")
				mrt.write("moe-moe-kyun")
				mrt.close()
				printLog(log, "MAXSCRIPT " + scriptDst)
				subprocess.call([ Max, "-U", "MAXScript", "shape_export.ms", "-q", "-mi", "-mip" ])
				if os.path.exists(outputLogfile):
					try:
						lSrc = open(outputLogfile, "r")
						for line in lSrc:
							lineStrip = line.strip()
							if (len(lineStrip) > 0):
								printLog(log, lineStrip)
						lSrc.close()
						os.remove(outputLogfile)
					except Exception:
						printLog(log, "ERROR Failed to read 3dsmax log")
				else:
					printLog(log, "WARNING No 3dsmax log")
				tagList = findFiles(log, tagDirectory, "", ".max.tag")
				newTagLen = len(tagList)
				tagDiff = newTagLen - tagLen
				tagLen = newTagLen
				addTagDiff = 0
				if os.path.exists(maxRunningTagFile):
					printLog(log, "FAIL 3ds Max crashed and/or file export failed!")
					if tagDiff == 0:
						if zeroRetryLimit > 0:
							zeroRetryLimit = zeroRetryLimit - 1
							addTagDiff = 1
						else:
							printLog(log, "FAIL Retry limit reached!")
					else:
						addTagDiff = 1
					os.remove(maxRunningTagFile)
				printLog(log, "Exported " + str(tagDiff) + " .max files!")
				tagDiff += addTagDiff
			os.remove(scriptDst)
	printLog(log, "")

log.close()
if os.path.isfile("log.log"):
	os.remove("log.log")
shutil.move("temp_log.log", "log.log")


# end of file
