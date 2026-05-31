# Hw5
12533581 苏子钰

本次作业尝试实现了四种block based的运动估计算法。以下是在search window = 15x15，在akiyo_cif, bowing_cif 与 container_cif下得到的结果。参考维基百科，作业中实现的Diamond Search是分大小diamond的diamon search，做出改动原本期望能削减MSE峰值，但是实际并未达到期望的结果。

![15_plot_aki](C:\Users\edsu\workdir\Digital_Image_Compression\written assignments\hw5\15_plot_aki.png)

![15_plot_bow](C:\Users\edsu\workdir\Digital_Image_Compression\written assignments\hw5\15_plot_bow.png)

![15_plot_cargo](C:\Users\edsu\workdir\Digital_Image_Compression\written assignments\hw5\15_plot_cargo.png)

![15_table](C:\Users\edsu\workdir\Digital_Image_Compression\written assignments\hw5\15_table.png)

第一个表说明了几个算法都能取得接近的性能，这和理论预期是相符的，表二说明带有初始位置的Diamond Search在部分场景下会相较于不带初始位置的有一定的提升。

可以注意到在akiyo中，MSE都处于一个较为正常的值，而在bowing和container下，会出现MSE尖峰。最直观的想法是，记录算法每一步的输入和输出block，这样就能比较直观的了解到为什么会出现尖峰。

对于较为正常的akio，选取其MSE最大的两帧对比：

![image-20260527232626606](C:\Users\edsu\AppData\Roaming\Typora\typora-user-images\image-20260527232626606.png)

![image-20260527232653443](C:\Users\edsu\AppData\Roaming\Typora\typora-user-images\image-20260527232653443.png)

以上是放大的块图像，可以注意到两者的差距不大，这和MSE较低的观测相符合。而在bowing中，

![image-20260527232808755](C:\Users\edsu\AppData\Roaming\Typora\typora-user-images\image-20260527232808755.png)

可以很明显的观测到表示运动估计的框并没有出现在应该出现的地方，由头发直接变成了背景

![image-20260527232912674](C:\Users\edsu\AppData\Roaming\Typora\typora-user-images\image-20260527232912674.png)

这带来了很大的MSE，通过查看运动向量的值`(7,-7)`可知，算法触碰到了搜索边界仍然无法找到最优匹配，帧间运动超出了search window可以捕捉的范围。因此，可以通过增大search window来获得比较好的结果，遗憾的是，在增加到30之后，只获取了较原来少有提升的结果，而无法削减峰值。而再次增大search range可能会导致整体运行时间过长。

另外，作业采用meidan mv的方式提供搜索先验，meidan mv通过计算过的邻近块的mv取中位数以获得对当前块的初始估计。实验结果表明，在特定帧的搜索中，先验可以明显地降低所需的计算步数。

