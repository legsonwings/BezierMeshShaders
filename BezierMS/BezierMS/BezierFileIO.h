#pragma once

#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <exception>

#include "BezierMaths.h"

namespace BezierFileIO
{
    template<unsigned N>
    BezierMaths::BezierTriangle<N> ParsePatch(std::string& inputString)
    {
        std::stringstream stream(inputString);
        BezierMaths::BezierTriangle<N> ret;
        std::string str;

        std::getline(stream, str, '{');
        std::getline(stream, str, '{');

        for (int i = 0; i < BezierMaths::BezierTriangle<N>::NumControlPoints; ++i)
        {
            std::getline(stream, str, '{');
            char floatChar;
            char separator;
            float x, y, z;
            stream >> x >> floatChar >> separator >> y >> floatChar >> separator >> z;
            std::getline(stream, str, '}');

            ret.ControlPoints[i] = { x, y, z };
        }

        return ret;
    }

    template<unsigned N>
    BezierMaths::BezierTriangle<N> ReadFromFile(std::wstring const& filePath)
    {
        std::ifstream file;
        file.open(filePath);

        std::string fileContents;
        std::string line;

        while (file >> line)
        {
            fileContents += line;
        }

        fileContents.erase(remove_if(fileContents.begin(), fileContents.end(), isspace), fileContents.end());

        return ParsePatch<N>(fileContents);
    }

    template<unsigned N>
    void WriteToFile(std::wstring const& filePath, BezierMaths::BezierTriangle<N> const& patch)
    {
        if (std::filesystem::exists(std::filesystem::path(filePath)))
        {
            std::wstringstream errorStream;
            errorStream << "File " << filePath << " already exists.";
            throw std::runtime_error(errorStream.str());
        }

        std::ofstream file(filePath);
        file << "{{\n";

        char const floatChar = 'f';
        std::string const separator = ", ";

        float const temp = (sqrt(patch.NumControlPoints * 8.f + 1.f) - 1.f) / 2.0f - 0.000001f;
        unsigned const NumRows = static_cast<unsigned int>(BezierMaths::ceil(temp));

        for (unsigned row = 0; row < NumRows; ++row)
        {
            for (unsigned i = 0; i < row + 1; ++i)
            {
                BezierMaths::ControlPoint const & ctrlPoint = patch.ControlPoints[(row * (row + 1)) / 2 + i];
                file << "{" << std::fixed << std::setprecision(4) << ctrlPoint.x << floatChar << separator << ctrlPoint.y << floatChar << separator << ctrlPoint.z << floatChar << "}";

                std::string delimeter = separator;

                // Only comma for the last control point of the row
                if (i == row)
                {
                    delimeter = ",";
                }

                // No delimiter for the last control point of the patch
                if (i == 3)
                {
                    delimeter = "";
                }

                file << delimeter;
            }

            // New line per row
            file << "\n";
        }

        file << "}}";

        file.close();
    }
}