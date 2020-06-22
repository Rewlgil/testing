#define numreading 500

template<class T>
class MovingAverage
{
  public:
    void giveAVGValue(T getdata);
    void getAVGValue(float *avg_value);
  private:
    T value[numreading] = {0};
    T sum = 0;
    uint16_t index = 0;
    boolean first_time = true;
};

template<class T>
void MovingAverage<T>::giveAVGValue(T getdata)
{
  sum -= value[index];
  value[index] = getdata;
  sum += value[index];
//******print value for debug***********************
//  for (uint16_t i = 0; i < numreading; i++)
//  {
//    Serial.print(value[i]);   if(i == index) Serial.print("*"); Serial.print("\t");
//    if ((i % 20) == 19)       Serial.print("\n");
//  }
//  Serial.print("\n");
//**************************************************
  index++;
  if (index >= numreading)  {index = 0; first_time = false;}
}

template<class T>
void MovingAverage<T>::getAVGValue(float *avg_value)
{
  if (first_time == true)
    *avg_value = (float)sum / index;
  else
    *avg_value = (float)sum / numreading;
//******print value for debug***********************
  for (uint16_t i = 0; i < numreading; i++)
  {
    Serial.print(value[i]);   if(i == index) Serial.print("*");   Serial.print("\t");
    if ((i % 20) == 19)       Serial.print("\n");
  }
  Serial.print("\n");
//**************************************************
}
