#include <stdio.h>
#include <string.h>
#include <windows.h>

#include <iostream>
#include <iterator>
#include <algorithm>
#include <boost/filesystem.hpp>
#include <set>
#include "option.h"

using namespace std;
using namespace boost::filesystem;

//xxxsteve - I hate this, I want to return reasons for failed
//file comparisons without using exceptions, but we'll see if
//I need to just bite the bullet and throw exceptions
#define SUCCESS                  0
#define ERROR_FILE_SIZE          1
#define ERROR_UNABLE_TO_OPEN_1   2
#define ERROR_UNABLE_TO_OPEN_2   3
#define ERROR_GETFILESIZE        4
#define ERROR_UNABLE_TO_READ_1   5
#define ERROR_UNABLE_TO_READ_2   6
#define ERROR_COMPARE_ERROR      7

#define BUFFER_SIZE 64*1024
#define NUM_THREADS 2

struct Options 
{
   string left;
   string right;
   bool verboseFlag;
   bool leftOnlyFlag;
   bool rightOnlyFlag;

   Options():verboseFlag(false), leftOnlyFlag(false), rightOnlyFlag(false) {};
};

Options o;

struct RecurseDirectoryInfo
{
   path p;
   set<string> *fileList;
   int numFiles;
   int numDirectories;

   RecurseDirectoryInfo():fileList(NULL), numFiles(0), numDirectories(0) {};
};

void GetOptionFlags(int argc, char *argv[], Options &o) 
{
   //get all the options
   char directoryName[2048];
   if (scan_option(argc, argv, "-left %s", directoryName) != 0)
      o.left = directoryName;

   if (scan_option(argc, argv, "-right %s", directoryName) != 0)
      o.right = directoryName;

   o.verboseFlag = scan_option(argc, argv, "-verbose") != 0;

   o.leftOnlyFlag = scan_option(argc, argv, "-leftOnly") != 0;
   o.rightOnlyFlag = scan_option(argc, argv, "-rightOnly") != 0;
}

int CompareFiles(string filename1, string filename2, char *buffer1, char *buffer2, int bufferSize)
{
   wstring wString1;
   wstring wString2;

   //convert strings into wstrings because of Windows
   std::copy(filename1.begin(), filename1.end(), std::back_inserter(wString1));
   std::copy(filename2.begin(), filename2.end(), std::back_inserter(wString2));

   LPCTSTR lpFileName1 = wString1.c_str();
   LPCTSTR lpFileName2 = wString2.c_str();

   HANDLE h1 = 0, h2 = 0;
   int errorCode = SUCCESS;

   //get the handles to each file
   h1 = CreateFile(lpFileName1, FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
   if (h1 == INVALID_HANDLE_VALUE) 
   {
      errorCode = ERROR_UNABLE_TO_OPEN_1;
      goto end;
   }
   h2 = CreateFile(lpFileName2, FILE_READ_DATA, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
   if (h2 == INVALID_HANDLE_VALUE) 
   {
      errorCode = ERROR_UNABLE_TO_OPEN_2;
      goto end;
   }

   //get the file sizes
   LARGE_INTEGER largeFileSize;
   if (GetFileSizeEx(h1, &largeFileSize) == 0)
   {
      errorCode = ERROR_GETFILESIZE;
      goto end;
   }
   __int64 fileSize1 = largeFileSize.QuadPart;

   if (GetFileSizeEx(h2, &largeFileSize) == 0)
   {
      errorCode = ERROR_GETFILESIZE;
      goto end;
   }
   __int64 fileSize2 = largeFileSize.QuadPart;

   if (fileSize1 != fileSize2)
   {
      errorCode = ERROR_FILE_SIZE;
      goto end;
   }

   bool showLargeFileFlag = false;
   if (fileSize1 > 50*1024*1024)
   {
      showLargeFileFlag = true;
      cout <<"\n" << filename1 << " is " << fileSize1 << " bytes" << endl;
   }

   DWORD numBytesToRead = bufferSize;

   LARGE_INTEGER frequency;
   QueryPerformanceFrequency(&frequency);

   LARGE_INTEGER count1;
   LARGE_INTEGER count2;

   DWORD numBytesRead = 0;

   __int64 remaining = fileSize1;
   int numReads = 0;
   __int64 avg = 0;
   bool returnValue = false;

   __int64 completed = 0;
   while (1)
   {
      QueryPerformanceCounter(&count1);
      BOOL rc = ReadFile(h1, buffer1, numBytesToRead, &numBytesRead, NULL);
      if (rc != TRUE)
      {
         errorCode = ERROR_UNABLE_TO_READ_1;
         goto end;
      }

      if (rc == TRUE && numBytesRead == 0)
      {
         if (showLargeFileFlag == true)
            cout << endl;

         errorCode = SUCCESS;
         goto end;
      }

      rc = ReadFile(h2, buffer2, numBytesToRead, &numBytesRead, NULL);
      if (rc != TRUE)
      {
         errorCode = ERROR_UNABLE_TO_READ_2;
         goto end;
      }

      QueryPerformanceCounter(&count2);
      __int64 readTime = count2.QuadPart - count1.QuadPart;
      avg = (avg * numReads + readTime)/(numReads + 1);
      numReads++;

      QueryPerformanceCounter(&count1);
      for (DWORD i = 0; i < numBytesToRead; i++)
      {
         if (buffer1[i] != buffer2[i])
         {
            cout << "not the same at offset " << i << endl;
            errorCode = ERROR_COMPARE_ERROR;
            goto end;
         }
      }
      QueryPerformanceCounter(&count2);
      __int64 compTime = count2.QuadPart - count1.QuadPart;

      completed += numBytesRead;
      if (o.verboseFlag == true || showLargeFileFlag == true)
      {
         printf("percent completed = %.2f\r", 100*((double)completed)/fileSize1);
      }
   }

end:

   CloseHandle(h1);
   CloseHandle(h2);

   return errorCode;
}

DWORD WINAPI RecurseDirectory(LPVOID lpParam)
{
   RecurseDirectoryInfo *info = (RecurseDirectoryInfo *) lpParam;

   path p = info->p;
   set<string> *fileList = info->fileList;

   if (is_directory(p) == false)
   {
      cout << p << " is not a directory!" << endl;
      exit(-1);
   }
   int originalPathLength = p.string().length();
   char lastChar = p.string()[p.string().length()- 1];
   if (lastChar != '/' && lastChar != '\\' && lastChar != ':')
      originalPathLength++;

   recursive_directory_iterator x = recursive_directory_iterator(p);
   while (x != recursive_directory_iterator())
   {
      directory_entry y = *x;
      path currentPath = y.path();
      if (is_directory(currentPath) == false)
      {
         string currentPathString = currentPath.string().substr(originalPathLength, currentPath.string().length() - originalPathLength);
         fileList->insert(currentPathString);
         info->numFiles++;
      }
      else
         info->numDirectories++;

      if (currentPath.filename() == "System Volume Information")
         x.no_push();

      ++x;
   }

   return 0;
}

int main(int argc, char* argv[])
{
   GetOptionFlags(argc, argv, o);

   if (o.left.empty() || o.right.empty())
   {
      //xxxsteve - lame help message
      cout << "fileComp -left <left directory> -right <right directory> [OPTIONS]" << endl;
      return 1;
   }


   try
   {
      set<string> fileList1;
      RecurseDirectoryInfo r1;
      path p1(o.left);
      r1.p = p1;
      r1.fileList = &fileList1;

      set<string> fileList2;
      RecurseDirectoryInfo r2;
      path p2(o.right);
      r2.p = p2;
      r2.fileList = &fileList2;

      //windows-only method to create a thread for now...
      HANDLE h[NUM_THREADS];
      h[0] = CreateThread(NULL, 0, RecurseDirectory, &r1, 0, NULL);
      h[1] = CreateThread(NULL, 0, RecurseDirectory, &r2, 0, NULL);
      while(1)
      {
         //wait for both threads to finish
         DWORD rc = WaitForMultipleObjects(NUM_THREADS, h, TRUE, 1000);
         if (rc == WAIT_TIMEOUT)
         {
            //if it's not finished, then keep printing output every 1 second
            printf("left = %d files scanned, right = %d files scanned\r", r1.numFiles, r2.numFiles);
         }
         else
         {
            printf("left = %d files scanned, right = %d files scanned\n", r1.numFiles, r2.numFiles);
            printf("Finished!\n");
            break;
         }
      }

      //if requested files that only exist in the left directory, then print them now
      if (o.leftOnlyFlag == true)
      {
         set<string> leftOnly;
         set_difference(fileList1.begin(), fileList1.end(), fileList2.begin(), fileList2.end(), inserter(leftOnly, leftOnly.begin()));
         set<string>::iterator leftOnlyIter;

         cout << "Left-only files:" << endl;
         for (leftOnlyIter = leftOnly.begin(); leftOnlyIter != leftOnly.end(); ++leftOnlyIter)
         {
            cout << "   " << *leftOnlyIter << endl;
         }
      }

      if (o.rightOnlyFlag == true)
      {
         set<string> rightOnly;
         set_difference(fileList2.begin(), fileList2.end(), fileList1.begin(), fileList1.end(), inserter(rightOnly, rightOnly.begin()));
         set<string>::iterator rightOnlyIter;

         cout << "Right-only files:" << endl;
         for (rightOnlyIter = rightOnly.begin(); rightOnlyIter != rightOnly.end(); ++rightOnlyIter)
         {
            cout << "   " << *rightOnlyIter << endl;
         }
      }

      //get the intersection of both files
      set<string> intersection;
      set_intersection(fileList1.begin(), fileList1.end(), fileList2.begin(), fileList2.end(), inserter(intersection, intersection.begin()));

      cout << p1 << " has " << fileList1.size() << " files" << endl;
      cout << p2 << " has " << fileList2.size() << " files" << endl;
      cout << "There are " << intersection.size() << " matches" << endl;

      set<string>::iterator iter;
      char *buffer1 = new char[BUFFER_SIZE];
      char *buffer2 = new char[BUFFER_SIZE];

      set<string> badFiles;

      int count = 0;
      for (iter = intersection.begin(); iter != intersection.end(); ++iter)
      {
         string path1 = o.left + "/" + *iter;
         string path2 = o.right + "/" + *iter;

         if (o.verboseFlag == true)
            cout << "Comparing " << path1 << " and " << path2 << endl;
         else
            printf("Comparing file %d\r", ++count);

         if (CompareFiles(path1, path2, buffer1, buffer2, BUFFER_SIZE) != SUCCESS)
           badFiles.insert(*iter);
      }

      delete buffer1;
      delete buffer2;

      cout << "Files that failed comparisons are:" << endl;

      set<string>::iterator badFilesIter;
      for (badFilesIter = badFiles.begin(); badFilesIter != badFiles.end(); ++badFilesIter)
      {
         cout << *badFilesIter << endl;
      }

   }
   catch (const filesystem_error& ex)
   {
      cout << ex.what() << '\n';
   }

   return 0;
}


/*
struct ReadFileStruct
{
   char *buffer;
   unsigned long bufferSize;
   HANDLE fileHandle;
   char *fileName;
   unsigned long numBytesToRead;
   unsigned long numBytesRead;
   HANDLE signalHandle;
   HANDLE waitHandle;
   int threadNumber;
   __int64 readTime;
};



int main(int argc, char *argv[])
{
   if (argc != 3)
      printf("fileComp file1 file2\n");

   LPCTSTR lpFileName1 = argv[1];
   LPCTSTR lpFileName2 = argv[2];

   HANDLE h1, h2;
   DWORD dwDesiredAccess = FILE_READ_DATA;
   DWORD dwShareMode = FILE_SHARE_READ;
   LPSECURITY_ATTRIBUTES lpSecurityAttributes = NULL;
   DWORD dwCreationDisposition = OPEN_EXISTING;
   DWORD dwFlagsAndAttributes = 0;
   HANDLE hTemplateFile = NULL;

   h1 = CreateFile(lpFileName1, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
   h2 = CreateFile(lpFileName2, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

   LARGE_INTEGER largeFileSize;
   GetFileSizeEx(h1, &largeFileSize);
   __int64 fileSize1 = largeFileSize.QuadPart;
   GetFileSizeEx(h2, &largeFileSize);
   __int64 fileSize2 = largeFileSize.QuadPart;
   if (fileSize1 != fileSize2)
   {
      printf("files do not match size\n");
      exit(-1);
   }

   printf("file1 = %I64d bytes, file2 = %I64d\n", fileSize1, fileSize2);


   DWORD numBytesToRead = BUFFER_SIZE;
   DWORD numBytesRead = 0;

   LARGE_INTEGER frequency;
   QueryPerformanceFrequency(&frequency);
   printf("frequency = %I64d\n", frequency.QuadPart);
   LARGE_INTEGER count1;
   LARGE_INTEGER count2;

   QueryPerformanceCounter(&count1);
   char *buff1 = (char *) calloc(1, numBytesToRead);
   QueryPerformanceCounter(&count2);
   __int64 calloc1 = count2.QuadPart - count1.QuadPart;
   printf("calloc took %I64d\n", calloc1);

   char *buff2 = (char *) calloc(1, numBytesToRead);

   __int64 remaining = fileSize1;
   
   HANDLE waitEventHandle = CreateEvent(NULL, TRUE, FALSE, "waitEvent");
   HANDLE signalEventHandle1 = CreateEvent(NULL, TRUE, FALSE, "sig1");
   HANDLE signalEventHandle2 = CreateEvent(NULL, TRUE, FALSE, "sig2");

   ReadFileStruct rs1;
   rs1.buffer = buff1;
   rs1.bufferSize = numBytesToRead;
   rs1.fileHandle = h1;
   rs1.fileName = (char *) lpFileName1;
   rs1.numBytesRead = 0;
   rs1.numBytesToRead = numBytesToRead;
   rs1.signalHandle = signalEventHandle1;
   rs1.waitHandle = waitEventHandle;
   rs1.threadNumber = 1;

   ReadFileStruct rs2;
   rs2.buffer = buff2;
   rs2.bufferSize = numBytesToRead;
   rs2.fileHandle = h2;
   rs2.fileName = (char *) lpFileName2;
   rs2.numBytesRead = 0;
   rs2.numBytesToRead = numBytesToRead;
   rs2.signalHandle = signalEventHandle2;
   rs2.waitHandle = waitEventHandle;
   rs2.threadNumber = 2;
   
   CreateThread(NULL, 0, ThreadReadFile, &rs1, 0, NULL);
   CreateThread(NULL, 0, ThreadReadFile, &rs2, 0, NULL);

   int num = 0;
   __int64 avg = 0;
   int numReads = 0;
   while (1)
   {
      QueryPerformanceCounter(&count1);

      WaitForSingleObject(signalEventHandle1, INFINITE);
      ResetEvent(signalEventHandle1);
      WaitForSingleObject(signalEventHandle2, INFINITE);
      ResetEvent(signalEventHandle2);

      QueryPerformanceCounter(&count2);
      __int64 readTime = count2.QuadPart - count1.QuadPart;

      avg = (avg * numReads + readTime)/(numReads + 1);
      numReads++;
      printf("avg for %d took %I64d\n", numReads, avg);
      
      QueryPerformanceCounter(&count1);

      if (rs1.numBytesRead == rs2.numBytesRead && rs1.numBytesRead == 0)
      {
         printf("finished\n");
         return 0;
      }

      for (unsigned long i = 0; i < rs1.numBytesRead; i++)
      {
         if (buff1[i] != buff2[i])
         {
            printf("not the same at offset %d\n", i);
            exit(-1);
         }
      }
      QueryPerformanceCounter(&count2);
      __int64 compTime = count2.QuadPart - count1.QuadPart;
      printf("compTime took %I64d\n\n", compTime);
      

      remaining -= numBytesToRead;
      printf("remaining = %I64d\n", remaining);

      SetEvent(waitEventHandle);
   }


   //printf("memoryBuffer = %s\n", memoryBuffer);
}
*/
