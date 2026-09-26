#ifndef DYNAMIC_ARRAY_H
#define DYNAMIC_ARRAY_H
#include <Arduino.h>
class Dynamic_Array
{ 
  public:
   Dynamic_Array();
   void NewArray_1D(byte _capacity_f);
   void IntNewArray_1D(byte _capacity_f);
   void CharNewArray_1D(byte _capacity_f);
   void NewArray_2D(byte _capacity_f, byte _capacity_c);
   void NewArray_3D(byte _capacity_f, byte _capacity_c, byte _capacity_p);
   void DeleteArray_1D();
   void IntDeleteArray_1D();
   void CharDeleteArray_1D();
   void DeleteArray_2D();
   void DeleteArray_3D();
   void WriteArray_1D(byte _pos_f,float _item);
   void IntWriteArray_1D(byte _pos_f,int _item);
   void CharWriteArray_1D(byte _pos_f,char* _item);
   void WriteArray_2D(byte _pos_f,byte _pos_c,float _item);
   void WriteArray_3D(byte _pos_f,byte _pos_c,byte _pos_p,float _item);
   float ReadArray_1D(byte _pos_f);
   int IntReadArray_1D(byte _pos_f);
   char* CharReadArray_1D(byte _pos_f);
   float ReadArray_2D(byte _pos_f,byte _pos_c);
   float ReadArray_3D(byte _pos_f,byte _pos_c,byte _pos_p);
   byte GetRows();
   byte GetColumns();
   byte GetDepth();
   void PrintArray_1D();
   void IntPrintArray_1D();
   void CharPrintArray_1D();
   void PrintArray_2D();
   void PrintArray_3D();
   int freeRam ();
   float MaxArray_1D();
   int IntMaxArray_1D();
   float MaxArray_2D();
   void ToinitializeArray_1D(float a);
   void IntToinitializeArray_1D(int a);
   void CharToinitializeArray_1D(char* a);
   void ToinitializeArray_2D(float a);
   void ToinitializeArray_3D(float a);
  //private:
   byte capacity_f;
   byte capacity_c;
   byte capacity_p;
  private:
   byte pos_f;
   byte pos_c;
   byte pos_p;
   float item;
   char* Charitem;
   float* Array1D;
   int* IntArray1D;
   char** CharArray1D;
   float** Array2D;
   float*** Array3D;
      
};
#endif

