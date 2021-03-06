#include <iostream>
#include <fstream>
#include <array>

using namespace std;

#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>

#include <windows.h>

#include "ctpl_stl.h"

#include "STLSlicer.h"

static bool slice(int threadID, STLSlicer &slicer, vector<cv::Mat*> slices, int z, fp_t zPosition);
static bool scaleAndCenter(STLGeometry& geometry, fp_t scale);

int main(int argc, char* argv[])
{
	// hard code bean values for now (assuming 0.04mm slice thickness)
	// TODO: get from a JSON config file (or command line?)
	array<size_t, 3> volSize = { 1440, 2560, 4000 };													// bean (pixels)
	Point3D voxelSize = { (fp_t)0.047, (fp_t)0.047, (fp_t)0.04 };										// bean (mm) (or should X,Y be buildVol / volSize)?
	Point3D buildVol = { (fp_t)68, (fp_t)120, (fp_t)160 };												// bean (mm)
	Point3D origin = { (fp_t)(volSize[0] / 2.0 - 0.5), (fp_t)(volSize[1] / 2.0 - 0.5), (fp_t)(-0.5) };	// fractional index

	ifstream stlStream(argv[1], std::ios::binary);

	STLGeometry geometry;
	if (!geometry.ReadBinary(stlStream))
	{
		cout << "error: can not read STL data" << endl;
		return -1;
	}
	//cin >> geometry;

	if (!scaleAndCenter(geometry, (fp_t)2.12))
	{
		cout << "error in geometry" << endl;
		return -1;
	}

	// need to create after all transformations are complete
	STLSlicer slicer(&geometry, volSize, buildVol, origin, voxelSize);

	bool parallel = true;
	if (parallel)
	{
		int threadCount = std::thread::hardware_concurrency();
		ctpl::thread_pool pool(threadCount);

		vector<cv::Mat*> slices;
		for (int i = 0; i < threadCount; i++)
			slices.push_back(new cv::Mat((int)volSize[1], (int)volSize[0], CV_8U));

		// https://stackoverflow.com/questions/15752659/thread-pooling-in-c11
		std::vector<std::future<bool>> results(volSize[2]);
		for (u_int z = 0; z < volSize[2]; z++)
		{
			fp_t zPosition = (z - origin[2]) * voxelSize[2];
			results[z] = pool.push(std::ref(slice), slicer, slices, z, zPosition);
		}
		for (u_int z = 0; z < volSize[2]; z++)
		{
			if (!results[z].get())
			{
				cout << "final slice: " << z << endl;

				break;
			}
		}
	}
	else
	{
		cv::Mat slice((int)volSize[1], (int)volSize[0], CV_8U);
		for (u_int z = 0; z < volSize[2]; z++)
		{
			if (!slicer.Slice(&slice, (z - origin[2]) * voxelSize[2]))
			{
				cout << "final slice: " << z << endl;

				break;
			}

			stringstream filename;
			// TODO: hardcode file name for now -- add to config file?
			filename << "slices\\slice" << std::setw(5) << std::setfill('0') << z << ".png";

			try
			{
				// TODO: optionally use morphological open/close operations to remove fine structures
				// TODO: is imwrite() thread safe?
				imwrite(filename.str().c_str(), slice);
				cout << filename.str() << endl;
			}
			catch (runtime_error& ex)
			{
				cerr << "Exception converting image to PNG format: " << ex.what() << endl;
				return false;
			}
		}
		cout << endl;
	}

	// done slicing
	// TODO: do whatever clean-up necessary (eg zip the directory)
	return 0;
}

static bool scaleAndCenter(STLGeometry& geometry, fp_t scale)
{
	geometry.Scale(scale, scale, scale);

	Point3D BBMin, BBMax;
	geometry.BoundingBox(BBMin, BBMax);

	fp_t offset[3];
	fp_t extent[3];
	for (int i = 0; i < 3; i++)
	{
		extent[i] = (BBMax[i] - BBMin[i]);

		if (i < 2)
			offset[i] = -BBMin[i] - extent[i]/2;		// center "x" and "y" axis over the origin
		else
			offset[i] = -BBMin[i];						// force "z" axis to zero
	}

	cout << "size of model: " << extent[0] << "mm x " << extent[1] << "mm x " << extent[2] << "mm" << endl;

	geometry.Translate(offset[0], offset[1], offset[2]);

	return true;
}

static bool slice(int threadID, STLSlicer &slicer, vector<cv::Mat*> slices, int z, fp_t zPosition)
{
	if (slicer.Slice(slices[threadID], zPosition))
	{
		stringstream filename;
		// TODO: hardcode file name for now -- add to config file?
		filename << "slices\\slice" << std::setfill('0') << std::setw(5) << z << ".png";

		try
		{
			// TODO: optionally use morphological open/close operations to remove fine structures
			// TODO: is imwrite() thread safe?
			imwrite(filename.str().c_str(), *(slices[threadID]));
		}
		catch (runtime_error& ex)
		{
			cerr << "Exception converting image to PNG format: " << ex.what() << endl;
			return false;
		}
	}
	return true;
}
