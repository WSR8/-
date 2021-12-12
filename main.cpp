#include<iostream>
#include<string>
#include<fstream>
#include<cmath>
#include<ctime>
#include<vector>
#include<algorithm>
using namespace std;

// 用于调试
//#define DEBUG true
// 用于绘制参数图
#define PLT true

#define random(x) rand()%x

// hyper-parameters
#define BAD 160       // 低于此分数的输出以对照
#define THRESHOLD 205 // 直接进入下一代的阈值
#define EPOCH 1000    // 训练周期
#define PERFECT 240   // 训练目标
#define CNTMUT 5     // 变异次数
#define CNTSP 1      // 特殊变换次数

#define MAXMUS 16     // 可以存储的最大音乐数
#define MAXPUT 2      // 可以输出的最多音乐数
#define LINES 3       // 格式：3行
#define MAXNOTE 32    // 格式：8分音符 4小节
#define CNTFILE 16    // 输入文件个数
#define HALF 8        // 半小节数量

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

#define MIN_BEST -(2 << 20)
int nowbest = MIN_BEST;                   // 记录当前最高分，用于搜索时剪枝
vector<chord_t> chordsmaj, chordsmin, temp; // 大调和小调时分别的功能组序列，作为额外信息打印
bool choose = true;                         // 最终选择大调还是小调，true为大调

// 计算和弦得分
namespace Cmaj
{
	// C大调的3个功能组
	const chord_t T = 'C'; // 主功能
	const chord_t S = 'F'; // 下属功能
	const chord_t D = 'G'; // 属功能
	// 根据单音猜测和弦
	void predict_chord(int pitch, int idx, vector<chord_t>& pred)
	{
		switch (pitch)
		{
		case 0:
			pred.emplace_back(T);
			pred.emplace_back(S);
			break;
		case 2:
			pred.emplace_back(D);
			pred.emplace_back(S);
			break;
		case 4:
			pred.emplace_back(T);
			break;
		case 5:
			pred.emplace_back(S);
			pred.emplace_back(D);
			break;
		case 6:
			pred.emplace_back(S);
			break;
		case 7:
			pred.emplace_back(T);
			pred.emplace_back(D);
			break;
		case 9:
			pred.emplace_back(S);
			break;
		case 11:
			pred.emplace_back(D);
			break;
		}
	}
}

namespace Amin
{
	// a小调的3个功能组
	const chord_t T('a'); // 主功能
	const chord_t S('d'); // 下属功能
	const chord_t D('E'); // 属功能
	// 根据单音猜测和弦
	void predict_chord(int pitch, int idx, vector<chord_t>& pred)
	{
		switch (pitch)
		{
		case 0:
			pred.emplace_back(T);
			break;
		case 2:
			pred.emplace_back(S);
			pred.emplace_back(D);
			break;
		case 4:
			pred.emplace_back(T);
			pred.emplace_back(D);
			break;
		case 5:
			pred.emplace_back(S);
			break;
		case 7:
			pred.emplace_back(T);
			pred.emplace_back(D);
			break;
		case 8:
			pred.emplace_back(D);
		case 9:
			pred.emplace_back(T);
			pred.emplace_back(S);
			break;
		case 11:
			pred.emplace_back(D);
			break;
		}
	}
};

// 计算和弦得分
void cal_chord_maj(const int beat[HALF], int idx, const chord_t* prev, double nowscore)
{
	if (idx >= HALF) // 搜到底
	{
		if (nowscore > nowbest) // 更新nowbest
			nowbest = nowscore, chordsmaj = temp;
		// nowscore = nowbest, chordsmaj = temp;
		return;
	}
	if (nowbest - nowscore > 16 - (idx << 1)) // 剪枝
		return;
	vector<chord_t> pred;
	bool found = true;
	chord_t maxchord;
	Cmaj::predict_chord(beat[idx], idx, pred);
	if (pred.empty()) // 找不到可能的和弦，-2分
	{
		if (!prev)
			return cal_chord_maj(beat, idx + 1, &Cmaj::T, nowscore - 2); // 上一个也为空，用主三代替
		return cal_chord_maj(beat, idx + 1, prev, nowscore - 2);         // 沿用上一个和弦
	}
	else if (prev && find(pred.begin(), pred.end(), *prev) == pred.end() && idx & 1)
		pred.emplace_back(*prev), found = false;
	// 对每种可能的和弦进行，都搜索后继
	for (const auto& ch : pred)
	{
		temp.emplace_back(ch);
		double score = 0;
		if (prev)
		{
			switch (*prev) // 根据上一个和弦确定和弦进行的给分
			{
			case Cmaj::T:
				if (ch == Cmaj::S)
					score += 2;
				else if (ch == Cmaj::D)
					score += 1;
				else
					score += 1.5;
				break;
			case Cmaj::S:
				if (ch == Cmaj::S)
					score += 1.5;
				else if (ch == Cmaj::D)
					score += 2;
				else
					score += 0.5;
				break;
			case Cmaj::D:
				if (ch == Cmaj::T)
					score += 2;
				else if (ch == Cmaj::S)
					score -= 2;
				else
					score += 0.5;
				break;
			}
		}
		if (prev && *prev == ch && !found)
			score -= 0.5;
		if (idx & 1)
			score *= 0.75;
		else if (!(idx & 4))
			score *= 1.5;
		cal_chord_maj(beat, idx + 1, &ch, nowscore + score);
		temp.pop_back();
	}
}
void cal_chord_min(const int beat[HALF], int idx, const chord_t* prev, double nowscore)
{
	if (idx >= HALF) // 搜到底
	{
		if (nowscore > nowbest) // 更新nowbest
			nowbest = nowscore, chordsmin = temp;
		// nowscore = nowbest, chordsmin = temp;
		return;
	}
	if (nowbest - nowscore > 16 - (idx << 1)) // 剪枝
		return;
	vector<chord_t> pred;
	bool found = true;
	chord_t maxchord;
	Amin::predict_chord(beat[idx], idx, pred);
	if (pred.empty()) // 找不到可能的和弦
	{
		if (!prev)
			return cal_chord_min(beat, idx + 1, &Amin::T, nowscore - 2); // 上一个也为空，用主三代替
		return cal_chord_min(beat, idx + 1, prev, nowscore - 2);         // 沿用上一个和弦
	}
	else if (prev && find(pred.begin(), pred.end(), *prev) == pred.end() && idx & 1)
		pred.emplace_back(*prev), found = false;
	// 对每种可能的和弦进行，都搜索后继
	for (const auto& ch : pred)
	{
		temp.emplace_back(ch);
		double score = 0;
		if (prev)
		{
			switch (*prev) // 根据上一个和弦确定和弦进行的给分
			{
			case Amin::T:
				if (ch == Amin::S)
					score += 2;
				else if (ch == Amin::D)
					score += 1;
				else
					score += 0.5;
				break;
			case Amin::S:
				if (ch == Amin::S)
					score += 1.5;
				else if (ch == Amin::D)
					score += 2;
				else
					score += 1;
				break;
			case Amin::D:
				if (ch == Amin::S)
					score -= 1;
				else if (ch == Amin::T)
					score += 1;
				else
					score += 0.5;
				break;
			}
		}
		if (prev && *prev == ch && !found)
			score -= 0.5;
		if (idx & 1)
			score *= 0.75;
		else if (!(idx & 4))
			score *= 1.5;
		cal_chord_min(beat, idx + 1, &ch, nowscore + score);
		temp.pop_back();
	}
}

static const double eff[] = { 1, 0.6, 0.8, 0.4, 0.9, 0.5, 0.7, 0.3 };

/* 适应度函数 */
double fitness(int music[LINES][MAXNOTE])
{
	double grade1_maj = 0, grade1_min = 0;
	double grade2 = 0;
	double grade3_maj = 0, grade3_min = 0;
	double grade4 = 0;

	// 音阶外音
	for (int i = 0; i < MAXNOTE; ++i)
		switch (music[0][i])
		{
		case 0:
		case 2:
		case 4:
		case 5:
		case 9:
			grade1_maj += 5 * eff[i & 7];
			grade1_min += 4 * eff[i & 7];
			break;
		case 6:
			grade1_maj += 1 * eff[i & 7];
			grade1_min += 1 * eff[i & 7];
			break;
		case 7:
			grade1_maj += 5 * eff[i & 7];
			grade1_min += 4 * eff[i & 7];
			break;
		case 8:
			grade1_maj -= 2 * eff[i & 7];
			grade1_min += 4 * eff[i & 7];
			break;
		case 10:
			grade1_maj -= 2 * eff[i & 7];
			grade1_min -= 5 * eff[i & 7];
			break;
		case 11:
			grade1_maj += 4 * eff[i & 7];
			grade1_min += 5 * eff[i & 7];
			break;
		case 12:
			grade1_maj += 3.5 * eff[i & 7];
			grade1_min += 3.5 * eff[i & 7];
			break;
		default:
			grade1_maj -= 5 * eff[i & 7];
			grade1_min -= 5 * eff[i & 7];
			break;
		}

	// 异常音程
	bool seventh = false;
	int cnt_jump = 0, same_len = 0;
	int each_diff[MAXNOTE] = { 0 };
	for (int i = 0; i < MAXNOTE; ++i)
	{
		int nextpitch = (i + 1) & 31;
		if (music[0][i] == 12 || music[0][nextpitch] == 12)
		{
			if (++same_len > 4)
				grade2 -= 1 * eff[i & 7];
			continue;
		}
		int diff = (music[1][nextpitch] * 12 + music[0][nextpitch]) - (music[1][i] * 12 + music[0][i]);
		if (!diff)
			each_diff[i] = 0;
		else if (diff >= 1 && diff <= 4)
			each_diff[i] = 1;
		else if (diff >= 5 && diff <= 9)
			each_diff[i] = 2;
		else if (diff >= 10 && diff <= 12)
			each_diff[i] = 3;
		else if (diff >= 13)
			each_diff[i] = 4;
		else if (diff <= -1 && diff >= -4)
			each_diff[i] = -1;
		else if (diff <= -5 && diff >= -9)
			each_diff[i] = -2;
		else if (diff <= -10 && diff >= -12)
			each_diff[i] = -3;
		else if (diff <= -13)
			each_diff[i] = -4;
		switch (abs(diff))
		{
		case 0:
			if (++same_len > 4)
				grade2 -= 0.5;
			if (same_len > 8)
				grade2 -= 1;
			grade2 += 3.5 * eff[i & 7];
			break;
		case 1:
			grade2 -= 3 * eff[i & 7];
			break;
		case 2:
			grade2 += 3.5 * eff[i & 7];
			break;
		case 3:
		case 4:
			grade2 += 5 * eff[i & 7];
			break;
		case 11:
			grade2 -= 1 * eff[i & 7];
			if (seventh)
				grade2 -= 1.5;
			seventh = true;
			break;
		case 10:
			grade2 += 1 * eff[i & 7];
			if (seventh)
				grade2 -= 1.5;
			seventh = true;
			if (++cnt_jump > 5)
				grade2 -= 1;
			break;
		case 5:
		case 7:
			if (++cnt_jump > 5)
				grade2 -= 1;
			grade2 += 6 * eff[i & 7];
			break;
		case 8:
		case 9:
			if (++cnt_jump > 5)
				grade2 -= 1;
			grade2 += 4.5 * eff[i & 7];
			break;
		case 6:
			grade2 -= 6 * eff[i & 7];
			if (++cnt_jump > 5)
				grade2 -= 1;
			break;
		case 12:
			if (++cnt_jump > 5)
				grade2 -= 1;
			if (++same_len > 4)
				grade2 -= 0.5;
			if (same_len > 8)
				grade2 -= 1;
			grade2 += 3 * eff[i & 7];
			break;
		default:
			grade2 -= 6 * eff[i & 7];
			if (seventh)
				grade2 -= 1.5;
			seventh = true;
			if (++cnt_jump > 5)
				grade2 -= 1;
			break;
		}
		if (!(i & 7))
			seventh = 0, cnt_jump = 0;
		if (diff % 12)
			same_len = 0;
	}

	// 节奏结构相似
	for (int i = 0; i < 8; i++)
	{
		grade4 += (music[2][i] > 0) == (music[2][i + 8] > 0);
		grade4 += (music[2][i + 16] > 0) == (music[2][i + 24] > 0);
	}
	for (int i = 0; i < 16; i++)
		grade4 += (music[2][i] > 0) == (music[2][i + 16] > 0);

	// 旋律结构相似
	int same = 0, rev = 0;
	for (int i = 0; i < 8; i++)
	{
		same += each_diff[i] == each_diff[i + 8];
		same += each_diff[i + 16] == each_diff[i + 24];
		rev += each_diff[i] == -each_diff[i + 8];
		rev += each_diff[i + 16] == -each_diff[i + 24];
	}
	for (int i = 0; i < 16; i++)
	{
		same += each_diff[i] == each_diff[i + 16];
		rev += each_diff[i] == -each_diff[i + 16];
	}
	grade4 = 0.25 * grade4 + 0.75 * max(same, rev);

	// 和弦推测
	int beat[HALF];
	for (int i = 0; i < MAXNOTE; i += 4)
		beat[i >> 2] = music[0][i];
	nowbest = MIN_BEST;
	cal_chord_maj(beat, 0, nullptr, 0);
	grade3_maj = nowbest;
	nowbest = MIN_BEST;
	cal_chord_min(beat, 0, nullptr, 0);
	grade3_min = nowbest;

	double score = max(grade1_maj + grade3_maj * 4, grade1_min + grade3_min * 4) + grade2 * 0.7 + grade4;
	return score;
}

/* 计算被选中遗传的概率，以200为基准，将得分缩放后使用softmax计算概率 */
void cal_probability()
{
	double sum = 0;
	for (int i = 0;i < MAXMUS;++i)
	{
		fit_val[i] /= 200;
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
int duplication(int threshold, int num)
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
		for (int j = start + len;j < MAXNOTE;++j)
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
}

/* 对son产生随机产生变异 */
void mutation()
{
	int index = random(MAXMUS);
	int case_ = random(4);

	/* 进行四种特殊变化 */
	/* 0——修改某个半拍。随机选取某个半拍，移动一个八度以内的随机大小，然后取
	   模并调整八度，之后修正相关延音 */
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
			if (temp == 1 && music_origin[index][1][choose] == 2) {}
			else
			{
				music_origin[index][1][choose] += temp;
			}
		}

		/* 修正延音部分 */
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

		/* 选取一整个音符 */
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
		
		/* 修改一整个音符（依赖延音），类似于修改某个半拍，但选取的是一个半拍
		   及其延音，整体调整，不需要修正延音 */
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
		/* 修改某个音对应的八度，随机选取一个半拍及其延音，将其八度进行一个±1
		   的变化，变化后的音域仍在0-2之间 */
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
	/* 节奏调整，将某个半拍向前或向后延长一个半音的长度，覆盖掉对应位置的音 */
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
	case 0:invert();break;         // 倒影
	case 1:reverse();break;        // 逆行
	case 2:transposition();break;  // 移调
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
			outFile << music_origin[res][i][j] << " ";
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

/* 将son中数据更新到origin，使用深拷贝 */
void update()
{
	for (int i = 0;i < MAXMUS;++i)
	{
		deepcopy(music_son[i], music_origin[i]);
	}
	check();
}

/* 获取每个epoch的最优分数 */
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

/* 记录每个epoch的训练情况，用于可视化处理 */
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

	/* 训练EPOCH次，训练流程为：首先检查父代，如果有大于THRESHOLD的父代直接放
	   进子代，之后对所有父代随机变异，然后计算每个父代的权重，以此生成每个父
	   代的选择概率进行交叉遗传，最后做特殊变化。一个epoch完成后，将子代全部
	   更新进父代 */
	int t;
	for (t = 0;t < EPOCH;++t)
	{

		cnt_son = 0;
		result = -1;

		// 如果得分已达到期望，则停止训练
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
	for (p = grad; p < grad + EPOCH; p = p + 5)
	{
		outF << p->epoch << "," << p->grade << endl;
	}
	outF.close();
#endif

	// 输出训练后的最优结果
	output(result, t);
}
