#include "Dynamic_Array.h"

Dynamic_Array::Dynamic_Array()
{
  capacity_f=1;
  capacity_c=1;
  capacity_p=1;
}

   void Dynamic_Array::NewArray_1D(byte _capacity_f)
   {
    capacity_f=_capacity_f;
    Array1D = new float[capacity_f]; 
   }

   void Dynamic_Array::IntNewArray_1D(byte _capacity_f)
   {
    capacity_f=_capacity_f;
    IntArray1D = new int[capacity_f]; 
   }


   void Dynamic_Array::CharNewArray_1D(byte _capacity_f)
   {
    capacity_f=_capacity_f;
    CharArray1D = new char*[capacity_f]; 
   }



   void Dynamic_Array::NewArray_2D(byte _capacity_f, byte _capacity_c)
   {
     capacity_f=_capacity_f;
     capacity_c=_capacity_c;
     Array2D = new float*[capacity_f];
     for (byte i = 0; i <capacity_f ; i++)
      {
        Array2D[i] = new float[capacity_c];
      }
   
   }
   
      void Dynamic_Array::NewArray_3D(byte _capacity_f,byte _capacity_c,byte _capacity_p)
   {
      capacity_f=_capacity_f;
      capacity_c=_capacity_c; 
      capacity_p=_capacity_p;
      Array3D = new float**[_capacity_f];
      for (byte i = 0; i <_capacity_f ; i++)
        {
          Array3D[i] = new float*[_capacity_c];
         for (byte j = 0; j <_capacity_c ; j++)
           {
            Array3D[i][j]=new float[_capacity_p];
           }
        }
   }
   
   void Dynamic_Array::DeleteArray_1D()
   {
    delete[] Array1D;
   }


  void Dynamic_Array::IntDeleteArray_1D()
   {
    delete[] IntArray1D;
   }
   


  void Dynamic_Array::CharDeleteArray_1D()
   {
    delete[] CharArray1D;
   }


   void Dynamic_Array::DeleteArray_2D()
   {
     for (byte i = 0; i < capacity_f; i++)
     {
      delete [] Array2D[i];
     }
     delete [] Array2D;
   }
   
   void Dynamic_Array::DeleteArray_3D()
   {
    for (byte i = 0; i <capacity_f ; i++)
    {
     for (byte j = 0; j <capacity_c ; j++)
     {
      delete [] Array3D[i][j];
      }
     delete [] Array3D[i];
     }
   delete [] Array3D;
   }

   void Dynamic_Array::WriteArray_1D(byte _pos_f,float _item)
   {
    pos_f=_pos_f;
    item=_item;
    Array1D[pos_f] = item;
   }
   

   void Dynamic_Array::IntWriteArray_1D(byte _pos_f,int _item)
   {
    pos_f=_pos_f;
    item=_item;
    IntArray1D[pos_f] = item;
   }

   void Dynamic_Array::CharWriteArray_1D(byte _pos_f,char* _Charitem)
   {
    pos_f=_pos_f;
    Charitem=_Charitem;
    CharArray1D[pos_f] = Charitem;
   }



   void Dynamic_Array::WriteArray_2D(byte _pos_f,byte _pos_c,float _item)
   {
    pos_f=_pos_f;
    pos_c=_pos_c;
    item=_item;
    Array2D[pos_f][pos_c] = item;
   }
   
   void Dynamic_Array::WriteArray_3D(byte _pos_f,byte _pos_c,byte _pos_p,float _item)
   {
    pos_f=_pos_f;
    pos_c=_pos_c;
    pos_p=_pos_p;
    item=_item;
    Array3D[pos_f][pos_c][pos_p] = item;
   }
   
   float Dynamic_Array::ReadArray_1D(byte _pos_f)
   {
    pos_f=_pos_f;
    return Array1D[pos_f];
   }


   int Dynamic_Array::IntReadArray_1D(byte _pos_f)
   {
    pos_f=_pos_f;
    return IntArray1D[pos_f];
   }

   char* Dynamic_Array::CharReadArray_1D(byte _pos_f)
   {
    pos_f=_pos_f;
    return CharArray1D[pos_f];
   }

   
   float Dynamic_Array::ReadArray_2D(byte _pos_f,byte _pos_c)
   {
     pos_f=_pos_f;
     pos_c=_pos_c;
    return Array2D[pos_f][pos_c];
   }
   
   float Dynamic_Array::ReadArray_3D(byte _pos_f,byte _pos_c,byte _pos_p)
   {
     pos_f=_pos_f;
     pos_c=_pos_c;
     pos_p=_pos_p;
    return Array3D[pos_f][pos_c][pos_p];
   }
   
   byte Dynamic_Array::GetRows()
   {
    return capacity_f;
   }
   
   byte Dynamic_Array::GetColumns()
   {
    return capacity_c;
   }
   
   byte Dynamic_Array::GetDepth()
   {
    return capacity_p;
   }
     
   void Dynamic_Array::PrintArray_1D()
   {
    for (byte index = 0; index < capacity_f; index++)
     {
      Serial.print(Array1D[index],4);
      Serial.print(' ');
     }
     Serial.println();
   }
   


   void Dynamic_Array::IntPrintArray_1D()
   {
    for (byte index = 0; index < capacity_f; index++)
     {
      Serial.print(IntArray1D[index]);
      Serial.print(' ');
     }
     Serial.println();
   }



   void Dynamic_Array::CharPrintArray_1D()
   {
    for (byte index = 0; index < capacity_f; index++)
     {
      Serial.print(CharArray1D[index]);
      Serial.print(' ');
     }
     Serial.println();
   }


   void Dynamic_Array::PrintArray_2D()
   {
    for (byte index_f = 0; index_f < capacity_f; index_f++)
      {
      for (byte index_c = 0; index_c < capacity_c; index_c++)
        {
         Serial.print(Array2D[index_f][index_c],4);
         Serial.print(' ');
        }
      Serial.println();
     }
   }
   
   void Dynamic_Array::PrintArray_3D()
   {
     for (byte index_p = 0; index_p < capacity_p; index_p++)
       { Serial.print("(:,:,");
         Serial.print(index_p);
        Serial.println(")");
        for (byte index_f = 0; index_f < capacity_f; index_f++)
          {
           for (byte index_c = 0; index_c < capacity_c; index_c++)
             {
              Serial.print(Array3D[index_f][index_c][index_p],4);
              Serial.print(' ');
             }
           Serial.println();
        
          }
       }
   }

   int Dynamic_Array::freeRam ()
{
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

float Dynamic_Array::MaxArray_1D()
{float maximo=0;
  for(byte i=0;i<capacity_f;i++){
     if(Array1D[i]>maximo){
                  maximo=Array1D[i];           
      }
}
return maximo;
}


int Dynamic_Array::IntMaxArray_1D()
{int maximo=0;
  for(byte i=0;i<capacity_f;i++){
     if(IntArray1D[i]>maximo){
                  maximo=IntArray1D[i];           
      }
}
return maximo;
}



float Dynamic_Array::MaxArray_2D()
{float maximo=0;

for(byte i=0;i<capacity_f;i++){
  for(byte j=0;j<capacity_c;j++){
     if(Array2D[i][j]>maximo){
                  maximo=Array2D[i][j];           
      }
}
}
return maximo;
}

void Dynamic_Array::ToinitializeArray_1D(float a)
{
    for (byte i = 0; i < capacity_f; i++)
   {
    Array1D[i]=a;
    } 

}


void Dynamic_Array::IntToinitializeArray_1D(int a)
{
    for (byte i = 0; i < capacity_f; i++)
   {
    IntArray1D[i]=a;
    } 

}


void Dynamic_Array::CharToinitializeArray_1D(char* a)
{
    for (byte i = 0; i < capacity_f; i++)
   {
    CharArray1D[i]=a;
    } 

}


void Dynamic_Array::ToinitializeArray_2D(float a)
{
 for (byte i = 0; i < capacity_c; i++)
   {for (byte j = 0; j < capacity_f; j++)
     { 
       Array2D[j][i]=a;
     }
    }
}

void Dynamic_Array::ToinitializeArray_3D(float a)
{
     for (byte k = 0; k < capacity_p; k++)
   {
     for (byte i = 0; i < capacity_c; i++)
        {
          for (byte j = 0; j < capacity_f; j++)
            {
               Array3D[j][i][k]=a;
            }
         }
   }     
}




