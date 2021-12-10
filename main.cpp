#include<iostream>
#include<string>
#include<fstream>
#include<cmath>
#include<ctime>
#include<vector>
#include<algorithm>
using namespace std;

//#define DEBUG true
//#define PLT true

#define random(x) rand()%x

// hyper-parameters
#define MAXMUS 23       // 可以存储的最大音乐数
#define LINES 3         // 格式：3行
#define MAXNOTE 32      // 格式：8分音符 4小节
#define THRESHOLD -15    // 直接进入下一代的阈值
#define CNTFILE 23      // 输入文件个数
#define EPOCH 1000      // 训练周期
#define PERFECT 12      // 训练目标
#define CNTMUT 10       // 变异次数
#define CNTSP 10        // 特殊变换次数
#define HALF 8          // 半小节数量

int music_origin[MAXMUS][LINES][MAXNOTE]; // 训练前数据
int music_son[MAXMUS][LINES][MAXNOTE];    // 训练后数据
double probability[MAXMUS];               // 被选中遗传的概率
double fit_val[MAXMUS];                   // 适应度值
int chord[HALF];                    // 记录每个半小节的大调和弦
int cnt_son = 0;                          // 下一个结果存放位置

/* 打印结果供调试使用 */
void print_note(int music[LINES][MAXNOTE])
{
	for (int i = 0;i < LINES;++i)
	{
		for (int j = 0;j < MAXNOTE;++j)
		{
			cout << music[i][j];
			if (j < MAXNOTE - 1)
				cout << ",";
		}
		cout << endl;
	}
}

/* 文件读取 index是存放位置 fileno是文件编号 */
void read_file(int index, int fileno) 
{
	string name = to_string(fileno) + ".txt";
	ifstream inFile;
	string str;
	int cnt = 0;
	
	inFile.open(name);
	if (!inFile)
	{
		cout << "open error!" << endl;
		return;
	}
	
	for (int i = 0;i < LINES;++i)
	{
		getline(inFile, str);
		str = str + '\n';
		for (int j = 0;str[j] != '\n';++j)
		{
			if (str[j] >= '0' && str[j] <= '9')
				music_origin[index][i][cnt] = music_origin[index][i][cnt] * 10 + str[j] - '0';
			if (str[j] == ',')
				cnt++;
		}
		cnt = 0;
	}
	inFile.close();
	//print_note(0);
}

typedef char chord_t;

namespace Cmaj
{
	// C大调的3个功能组
	const chord_t C = 'C'; // 主功能
	const chord_t F = 'F'; // 下属功能
	const chord_t G = 'G'; // 属功能
	// 根据单音猜测和弦
	void predict_chord(int pitch, int idx, vector<chord_t>& pred)
	{
		switch (pitch)
		{
		case 0:
			pred.emplace_back(C);
			pred.emplace_back(F);
			break;
		case 2:
			pred.emplace_back(G);
			pred.emplace_back(F);
			break;
		case 4:
			pred.emplace_back(C);
			break;
		case 5:
			pred.emplace_back(F);
			break;
		case 6:
			pred.emplace_back(F);
			break;
		case 7:
			pred.emplace_back(C);
			pred.emplace_back(G);
			break;
		case 9:
			pred.emplace_back(F);
			break;
		case 11:
			pred.emplace_back(G);
			break;
		}
	}
}

namespace Amin
{
	// a小调的3个功能组
	const chord_t Am('a'); // 主功能
	const chord_t Dm('d'); // 下属功能
	const chord_t E('E');  // 属功能
	// 根据单音猜测和弦
	void predict_chord(int pitch, int idx, vector<chord_t>& pred)
	{
		switch (pitch)
		{
		case 0:
			pred.emplace_back(Am);
			break;
		case 2:
			pred.emplace_back(Dm);
			pred.emplace_back(E);
			break;
		case 4:
			pred.emplace_back(Am);
			pred.emplace_back(E);
			break;
		case 5:
			pred.emplace_back(Dm);
			break;
		case 7:
			pred.emplace_back(Am);
			pred.emplace_back(E);
			break;
		case 8:
			pred.emplace_back(E);
		case 9:
			pred.emplace_back(Am);
			pred.emplace_back(Dm);
			break;
		case 11:
			pred.emplace_back(E);
			break;
		}
	}
};

#define MIN_BEST -(2 << 20)
int nowbest = MIN_BEST;                   // 记录当前最高分，用于搜索时剪枝
vector<chord_t> chordsmaj, chordsmin, temp; // 大调和小调时分别的功能组序列，作为额外信息打印
bool choose = true;                         // 最终选择大调还是小调，true为大调

// 计算和弦得分
void cal_chord_maj(const int beat[8], int idx, const chord_t* prev, int nowscore)
{
	if (idx >= 9) // 搜到底
	{
		if (nowscore > nowbest) // 更新nowbest
			nowbest = nowscore, chordsmaj = temp;
			//nowscore = nowbest, chordsmaj = temp;
		return;
	}
	if (nowbest - nowscore > 16 - 2 * idx) // 剪枝
		return;
	vector<chord_t> pred;
	int maxscore = MIN_BEST;
	chord_t maxchord;
	Cmaj::predict_chord(beat[idx], idx, pred);
	if (pred.empty()) // 找不到可能的和弦，-2分
	{
		if (!prev)
			return cal_chord_maj(beat, idx + 1, &Cmaj::C, nowscore - 2); // 上一个也为空，用主三代替
		return cal_chord_maj(beat, idx + 1, prev, nowscore - 2);         // 沿用上一个和弦
	}
	// 对每种可能的和弦进行，都搜索后继
	for (const auto& ch : pred)
	{
		temp.emplace_back(ch);
		int score = 0;
		if (prev)
		{
			switch (*prev) // 根据上一个和弦确定和弦进行的给分
			{
			case Cmaj::C:
				if (ch == Cmaj::C)
					score++;
				else if (ch == Cmaj::G)
					score += 2;
				break;
			case Cmaj::F:
				if (ch == Cmaj::G)
					score -= 2;
				else
					score++;
				break;
			case Cmaj::G:
				if (ch == Cmaj::C)
					score++;
				else if (ch == Cmaj::F)
					score += 2;
				break;
			}
		}
		cal_chord_maj(beat, idx + 1, &ch, nowscore + score);
		temp.pop_back();
	}
}
void cal_chord_min(const int beat[8], int idx, const chord_t* prev, int nowscore)
{
	if (idx >= 9) // 搜到底
	{
		if (nowscore > nowbest) // 更新nowbest
			nowbest = nowscore, chordsmaj = temp;
			//nowscore = nowbest, chordsmin = temp;
		return;
	}
	if (nowbest - nowscore > 16 - 2 * idx) // 剪枝
		return;
	vector<chord_t> pred;
	int maxscore = MIN_BEST;
	chord_t maxchord;
	Amin::predict_chord(beat[idx], idx, pred);
	if (pred.empty()) // 找不到可能的和弦
	{
		if (!prev)
			return cal_chord_min(beat, idx + 1, &Amin::Am, nowscore - 2); // 上一个也为空，用主三代替
		return cal_chord_min(beat, idx + 1, prev, nowscore - 2);          // 沿用上一个和弦
	}
	// 对每种可能的和弦进行，都搜索后继
	for (const auto& ch : pred)
	{
		temp.emplace_back(ch);
		int score = 0;
		if (prev)
		{
			switch (*prev) // 根据上一个和弦确定和弦进行的给分
			{
			case Amin::Am:
				if (ch == Amin::E)
					score += 2;
				else if (ch == Amin::Am)
					score++;
				break;
			case Amin::Dm:
				if (ch != Amin::E)
					score++;
				else
					score--;
				break;
			case Amin::E:
				if (ch == Amin::Dm)
					score += 2;
				else
					score++;
				break;
			}
		}
		cal_chord_min(beat, idx + 1, &ch, nowscore + score);
		temp.pop_back();
	}
}

int fitness(int music[LINES][MAXNOTE])
{
	int grade1_maj = 160, grade1_min = 160;
	int grade2 = 80;
	int grade3_maj = 0, grade3_min = 0;
	int grade4 = 0;

	// 音阶外音
	for (int i = 0; i < MAXNOTE; ++i)
		switch (music[0][i])
		{
		case 0:
		case 2:
		case 4:
		case 5:
		case 9:
			break;
		case 6:
			grade1_maj -= 5;
			grade1_min -= 4;
			break;
		case 7:
			grade1_min -= 2;
		case 8:
			grade1_maj -= 7;
			grade1_min--;
			break;
		case 10:
			grade1_maj -= 7;
			grade1_min -= 10;
		case 11:
			grade1_maj--;
			break;
		case 12:
			grade1_maj -= 2;
			grade1_min -= 2;
			break;
		default:
			grade1_maj -= 10;
			grade1_min -= 10;
			break;
		}

	// 异常音程
	bool seventh = false;
	int cnt_jump = 0, same_len = 0;
	for (int i = 0; i < MAXNOTE; ++i)
	{
		if (music[0][i] == 12 || music[0][(i + 1) & 31] == 12)
			continue;
		int diff = abs((music[1][(i + 1) & 31] * 12 + music[0][(i + 1) & 31]) - (music[1][i] * 12 + music[0][i]));
		switch (diff)
		{
		case 12:
			if (++cnt_jump > 5)
				grade2--;
		case 0:
			if (++same_len > 4)
				grade2--;
			break;
		case 1:
			grade2 -= 7;
			break;
		case 2:
			grade2 -= 2;
			break;
		case 3:
		case 4:
			grade2 -= 2;
			break;
		case 11:
			grade2 -= 5;
		case 10:
			grade2 -= 2;
			if (seventh)
				grade2--;
			seventh = true;
		case 5:
		case 7:
		case 8:
		case 9:
			if (++cnt_jump > 5)
				grade2--;
			break;
		case 6:
			if (++cnt_jump > 5)
				grade2--;
		default:
			grade2 -= 10;
			break;
		}
		if (!(i & 7))
			seventh = 0, cnt_jump = 0;
		if (diff % 12)
			same_len = 0;
	}

	// 节奏相似
	for (int i = 0; i < 8; i++)
	{
		grade4 += music[2][i] & music[2][i + 8];
		grade4 += music[2][i + 16] & music[2][i + 24];
	}
	for (int i = 0; i < 16; i++)
		grade4 += music[2][i] & music[2][i + 16];

	// 和弦推测
	int beat[8];
	for (int i = 0; i < MAXNOTE; i += 4)
		beat[i >> 2] = music[0][i];
	nowbest = MIN_BEST;
	cal_chord_maj(beat, 0, nullptr, 0);
	grade3_maj = nowbest << 2;
	nowbest = MIN_BEST;
	cal_chord_min(beat, 0, nullptr, 0);
	grade3_min = nowbest << 2;

	return max(grade1_maj + grade3_maj, grade1_min + grade3_min) + grade2 + grade4;
}

/* 计算被选中遗传的概率，softmax */
void cal_probability()
{
	double sum = 0;
	for (int i = 0;i < MAXMUS;++i)
	{
		fit_val[i] /= 100;
		fit_val[i] += 3;
		if (fit_val[i] < 0)
			printf("error!\n");
	}
	for (int i = 0;i < MAXMUS;++i)
	{
		sum += exp(fit_val[i]);
	}
	for (int i = 0;i < MAXMUS;++i)
	{
		if (i == 0)
			probability[i] = exp(fit_val[i]) / sum;
		else
			probability[i] = exp(fit_val[i]) / sum + probability[i - 1];
	}
}

/* 进行深拷贝 */
void deepcopy(int src[LINES][MAXNOTE], int dst[LINES][MAXNOTE])
{
	for (int i = 0;i < LINES;++i)
	{
		for (int j = 0;j < MAXNOTE;++j)
		{
			dst[i][j] = src[i][j];
		}
	}
}

/* 计算适应度，高于阈值的直接进son，达到要求的直接返回,
   num是origin数量,会将计算得到的适应度储存 */
int duplication(int threshold,int num) 
{
	for (int i = 0;i < num;++i)
	{
		fit_val[i] = fitness(music_origin[i]);
		if (fit_val[i] >= PERFECT)
		{
			return i;
		}
		if (fit_val[i] >= threshold)
		{
			deepcopy(music_origin[i], music_son[cnt_son++]);
		}
	}
	return -1;
}

/* 从origin产生交叉子代到son，在此处调整长度选取的概率 */
void crossover() 
{
	int f1, f2, start, len;
	double f1_rand, f2_rand;

	f1_rand = random(10000) / 10000.0;
	f2_rand = random(10000) / 10000.0;
	start = random(MAXNOTE);
	len = random(15);

	for (int i = 0;i < MAXMUS;++i)
	{
		if (f1_rand < probability[i])
		{
			f1 = i;
			break;
		}
	}
	for (int i = 0;i < MAXMUS;++i)
	{
		if (f2_rand < probability[i])
		{
			f2 = i;
			break;
		}
	}

	// 按概率选择长度
	if (len == 0)
	{ // 选奇数长度
		len = random(16);
		len = len * 2 + 1;
	}
	else if (len <= 2)
	{ // 选偶数但不是4的倍数
		len = random(9);
		if (len != 0)
		{
			len = (len * 2 - 1) * 2;
		}
	}
	else if (len <= 5)
	{ // 选偶数但不是8的倍数
		len = random(4);
		len = len * 8 + 4;
	}
	else if (len <= 9)
	{ // 选偶数但不是16的倍数
		len = random(2);
		len = len * 16 + 8;
	}
	else
	{ // 选16
		len = 16;
	}
	// 长度不能超过整体
	if (start + len > MAXNOTE)
	{
		len = MAXNOTE - start;
	}

	// 产生两个遗传子代
	int tenuto_f1, tenuto_f2;
	for (int i = 0;i < LINES;++i)
	{
		for (int j = 0;j < start;++j)
		{
			music_son[cnt_son][i][j] = music_origin[f1][i][j];
			music_son[cnt_son + 1][i][j] = music_origin[f2][i][j];
		}
	}
	for (int i = 0;i < LINES - 1;++i)
	{
		for (int j = start;j < start + len;++j)
		{
			music_son[cnt_son][i][j] = music_origin[f2][i][j];
			music_son[cnt_son + 1][i][j] = music_origin[f1][i][j];
		}
	}
	tenuto_f1 = music_origin[f1][2][start];
	tenuto_f2 = music_origin[f2][2][start];
	for (int j = start;j < start + len;++j)
	{
		if (music_origin[f1][2][j] == 0)
			tenuto_f1 = 0;
		if (music_origin[f2][2][j] == 0)
			tenuto_f2 = 0;
		music_son[cnt_son][2][j] = music_origin[f2][2][j] - tenuto_f2;
		music_son[cnt_son + 1][2][j] = music_origin[f1][2][j] - tenuto_f1;
	}
	for (int i = 0;i < LINES - 1;++i)
	{
		for (int j = start + len;j < MAXNOTE;++j)
		{
			music_son[cnt_son][i][j] = music_origin[f1][i][j];
			music_son[cnt_son + 1][i][j] = music_origin[f2][i][j];
		}
	}
	if (start + len < MAXNOTE)
	{
		tenuto_f1 = music_origin[f1][2][start + len];
		tenuto_f2 = music_origin[f2][2][start + len];
		for (int j = start+len;j <MAXNOTE;++j)
		{
			if (music_origin[f1][2][j] == 0)
				tenuto_f1 = 0;
			if (music_origin[f2][2][j] == 0)
				tenuto_f2 = 0;
			music_son[cnt_son][2][j] = music_origin[f1][2][j] - tenuto_f1;
			music_son[cnt_son + 1][2][j] = music_origin[f2][2][j] - tenuto_f2;
		}
	}

	cnt_son += 2;
#ifdef DEBUG
	cout << "father" << f1 << " " << f2 << endl;
	cout << "start " << start << " len " << len << endl;
	print_note(music_son[cnt_son - 2]);
	print_note(music_son[cnt_son - 1]);
#endif
}

/* 对son产生随机产生变异 */
void mutation()
{
	int index = random(MAXMUS);
	int case_ = random(4);
	if (case_ == 0)
	{
		int delta = random(23);
		int choose = random(MAXNOTE);
		int new_pitch;

		delta -= 11;
		new_pitch = music_origin[index][0][choose] + delta;
		if (new_pitch < 0)
		{
			music_origin[index][0][choose] = new_pitch + 12;
			if (music_origin[index][1][choose] > 0)
			{
				music_origin[index][1][choose]--;
			}
		}
		else
		{
			music_origin[index][0][choose] = new_pitch % 12;
			int temp = new_pitch / 12;
			if (temp == 1 && music_origin[index][1][choose] == 2){}
			else
			{
				music_origin[index][1][choose] += temp;
			}
		}

		music_origin[index][2][choose] = 0;
		if (choose + 1 < MAXNOTE)
		{
			int tenuto = music_origin[index][2][choose + 1];
			for (int i = choose + 1;i < MAXNOTE;++i)
			{
				if (music_origin[index][2][i] == 0)
					break;
				music_origin[index][2][i] -= tenuto;
			}
		}
	}
	else if (case_ == 1 || case_ == 2)
	{
		int delta;
		int choose, new_pitch, new_scale, len = 0;

		for (int i = 0;i < MAXNOTE;++i)
		{
			if (music_origin[index][2][i] == 0)
				len++;
		}
		choose = random(len);

		int cnt = -1;
		for (int i = 0;i < MAXNOTE;++i)
		{
			if (music_origin[index][2][i] == 0)
			{
				cnt++;
			}
			if (cnt == choose)
			{
				choose = i;
				break;
			}
		}

		if (case_ == 1)
		{
			delta = random(23) - 11;
			if (music_origin[index][0][choose] + delta < 0)
			{
				new_pitch = music_origin[index][0][choose] + delta + 12;
				new_scale = -1;
			}
			else
			{
				new_pitch = (music_origin[index][0][choose] + delta) % 12;
				new_scale = (music_origin[index][0][choose] + delta) / 12;
			}
			if (music_origin[index][1][choose] == 0 && new_scale == -1)
				new_scale = 0;
			if (music_origin[index][1][choose] == 2 && new_scale == 1)
				new_scale = 0;
			for (int i = choose;i < MAXNOTE;++i)
			{
				music_origin[index][0][i] = new_pitch;
				music_origin[index][1][i] += new_scale;
				if (i + 1 < MAXNOTE && music_origin[index][2][i + 1] == 0)
					break;
			}
		}
		else // case == 2
		{
			delta = random(2); // 等于0向下移动，等于1向上移动八度
			if (delta == 0 && music_origin[index][1][choose] != 0)
			{
				for (int i = choose;i < MAXNOTE;++i)
				{
					music_origin[index][1][i]--;
					if (i + 1 < MAXNOTE && music_origin[index][2][i + 1] == 0)
						break;
				}
			}
			if (delta == 1 && music_origin[index][1][choose] != 2)
			{
				for (int i = choose;i < MAXNOTE;++i)
				{
					music_origin[index][1][i]++;
					if (i + 1 < MAXNOTE && music_origin[index][2][i + 1] == 0)
						break;
				}
			}
		}
	}
	else if (case_ == 3)
	{
		int choose = random(MAXNOTE - 2) + 1;
		int dir = random(2); // 0向前延长，1向后延长
		if (dir == 0 && music_origin[index][2][choose] == 0)
		{ // 如果choose位置不是0，那么该音与前一个相同
			music_origin[index][0][choose - 1] = music_origin[index][0][choose];
			music_origin[index][1][choose - 1] = music_origin[index][1][choose];
			music_origin[index][2][choose - 1] = 0;
			music_origin[index][2][choose] = 1;
			if (choose + 1 < MAXNOTE)
			{
				int tenuto = music_origin[index][2][choose + 1];
				for (int i = choose + 1;i < MAXNOTE;++i)
				{
					if (music_origin[index][2][i] == 0)
						break;
					music_origin[index][2][i] -= tenuto;
				}
			}
		}
		else if (dir == 1 && music_origin[index][2][choose + 1] == 0)
		{ // 如果choose+1位置不是0，那么该音与后一个相同
			music_origin[index][0][choose + 1] = music_origin[index][0][choose];
			music_origin[index][1][choose + 1] = music_origin[index][1][choose];
			for (int i = choose + 1;i < MAXNOTE;++i)
			{
				music_origin[index][2][i] += music_origin[index][2][choose] + 1;
				if (i + 1 < MAXNOTE && music_origin[index][2][i + 1] == 0)
					break;
			}
		}
	}
}

/* 倒影 */
void invert()
{
	int index = random(MAXMUS);
	int delta = random(12);
	delta *= 2;

	for (int i = 0;i < MAXNOTE;++i)
	{
		int temp = delta - music_son[index][0][i];
		if (temp < 0)
		{
			music_son[index][0][i] = temp + 12;
			music_son[index][1][i] -= 1;
		}
		else if (temp >= 12)
		{
			music_son[index][0][i] = temp - 12;
			music_son[index][1][i] += 1;
		}
		else
		{
			music_son[index][0][i] = temp;
		}
	}

	int flag = 0;
	for (int i = 0;i < MAXNOTE;++i)
	{
		if (music_son[index][1][i] < 0)
		{
			flag = 1;
			break;
		}
		if (music_son[index][1][i] > 2)
		{
			flag = -1;
			break;
		}
	}
	if (flag != 0)
	{
		for (int i = 0;i < MAXNOTE;++i)
		{
			music_son[index][1][i] += flag;
		}
	}
}

/* 逆行 */
void reverse()
{
	int temp[LINES][MAXNOTE];
	int index = random(MAXMUS);
	for (int i = 0;i < LINES;++i)
	{
		for (int j = 0;j < MAXNOTE;++j)
		{
			temp[i][j] = music_son[index][i][MAXNOTE - j - 1];
		}
	}
	for (int i = 0;i < LINES;++i)
	{
		for (int j = 0;j < MAXNOTE;++j)
		{
			music_son[index][i][j] = temp[i][j];
		}
	}
	for (int i = 0;i < MAXNOTE;)
	{
		int cnt = 0;
		if (music_son[index][2][i] != 0)
		{
			while (music_son[index][2][i] != 0)
			{
				cnt++;
				i++;
			}
			cnt++;
			i++;
			for (int j = i - cnt, k = 0;j < i;++j, ++k)
			{
				music_son[index][2][j] = k;
			}
		}
		else
			++i;
	}
}

/* 移调 */
void transposition()
{
	int index = random(MAXMUS);
	int dir = random(2); // 0向上移调，1向下移调
	int delta = random(11) + 1;
	for (int i = 0;i < MAXNOTE;++i)
	{
		int temp = dir == 0 ? music_son[index][0][i] + delta : 
			music_son[index][0][i] - delta;
		if (temp > 11)
		{
			music_son[index][0][i] = temp - 12;
			music_son[index][1][i]++;
		}
		else if (temp < 0)
		{
			music_son[index][0][i] = temp + 12;
			music_son[index][1][i]--;
		}
		else
			music_son[index][0][i] = temp;
	}
	int flag = 0;
	for (int i = 0;i < MAXNOTE;++i)
	{
		if (music_son[index][1][i] < 0)
		{
			flag = 1;
			break;
		}
		else if (music_son[index][1][i] > 2)
		{
			flag = -1;
			break;
		}
	}
	if (flag != 0)
	{
		for (int i = 0;i < MAXNOTE;++i)
		{
			music_son[index][1][i] += flag;
		}
	}
}

/* 进行移调、倒影、逆行等特殊变换 */
void special()
{
	int case_ = random(3);
	switch (case_)
	{
	case 0:invert();break;
	case 1:reverse();break;
	case 2:transposition();break;
	default:break;
	}
}

/* 当训练结束后没有达到最优，查找次优结果 */
int find_maxresult()
{
	int max_iteration = 0;
	double max_result = MIN_BEST;
	double temp;
	for (int i = 0;i < MAXMUS;++i)
	{
		temp = fitness(music_origin[i]);
		if (temp > max_result)
		{
			max_iteration = i;
			max_result = temp;
		}
	}
	return max_iteration;
}

/* 输出训练结果 */
void output(int result, int t)
{
	ofstream outFile;
	int res = result;

	outFile.open("result.txt");
	if (!outFile)
	{
		cout << "open result error!" << endl;
		return;
	}

#ifdef DEBUG
	for (int index = 0;index < MAXMUS;++index)
	{
		outFile << "grade:" << fitness(music_origin[index]) << endl;
		for (int i = 0;i < LINES;++i)
		{
			for (int j = 0;j < MAXNOTE;++j)
			{
				outFile << music_origin[index][i][j] << " ";
			}
			outFile << endl;
		}
	}
#else
	if (res == -1)
	{
		res = find_maxresult();
	}
	outFile << "grade:" << fitness(music_origin[res]) << "; train epoch:" << t << endl;
	cout << "grade:" << fitness(music_origin[res]) << "; train epoch:" << t << endl;
	for (int i = 0;i < LINES;++i)
	{
		for (int j = 0;j < MAXNOTE;++j)
		{
			outFile << music_origin[res][i][j]<<" ";
		}
		outFile << endl;
	}
#endif

	outFile.close();
}

/* 检查origin中的内容 */
void check()
{
	for (int index = 0;index < MAXMUS;++index)
	{
		for (int node = 1;node < MAXNOTE;++node)
		{
			if (music_origin[index][2][node] != 0 && music_origin[index][2][node]
				== music_origin[index][2][node - 1])
			{
				music_origin[index][2][node]++;
			}
		}
	}
	for (int index = 0;index < MAXMUS;++index)
	{
		for (int node = 0;node < MAXNOTE;++node)
		{
			if (music_origin[index][1][node] < 0)
				music_origin[index][1][node] = 0;
			if (music_origin[index][1][node] > 2)
				music_origin[index][1][node] = 2;
		}
	}
}

/* 将son中数据更新到origin */
void update()
{
	for (int i = 0;i < MAXMUS;++i)
	{
		deepcopy(music_son[i], music_origin[i]);
	}
	check();
}

double plt()
{
	double maxn = MIN_BEST;
	for (int i = 0;i < MAXMUS;++i)
	{
		if (maxn < fit_val[i])
			maxn = fit_val[i];
	}
	return maxn;
}

typedef struct grades
{
	int epoch;
	double grade;
}Grad;
Grad grad[EPOCH];
Grad* p;

int main()
{
	int result = -1;

	srand((unsigned)time(0));

	for (int i = 1;i <= CNTFILE;++i)
		read_file(i - 1, i);

	int t;
	for (t = 0;t < EPOCH;++t)
	{
		
		cnt_son = 0;
		result = -1;

		result = duplication(THRESHOLD, CNTFILE);
#ifdef PLT
#else
		if (result != -1)
			break;
#endif

		grad[t].grade = plt();
		grad[t].epoch = t;
		
		for (int i = 0;i < CNTMUT;++i)
			mutation();
		cal_probability();
		for (int i = cnt_son;i < MAXMUS;i += 2)
			crossover();
		for (int i = 0;i < CNTSP;++i)
			special();

		update();
	}

#ifdef PLT
	ofstream outF;
	outF.open("table.csv", ios::out);
	outF << "epoch" << "," << "grade" << endl;
	for (p = grad; p < grad + EPOCH; p++)
	{
		outF << p->epoch << "," << p->grade << endl;
	}
	outF.close();
#endif

	output(result, t);
}
