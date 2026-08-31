namespace ServiceActivator;

partial class Form1
{
    private System.ComponentModel.IContainer components = null;

    protected override void Dispose(bool disposing)
    {
        if (disposing && (components != null))
        {
            components.Dispose();
        }
        base.Dispose(disposing);
    }

    private void InitializeComponent()
    {
        DataGridViewCellStyle dataGridViewCellStyle1 = new DataGridViewCellStyle();
        DataGridViewCellStyle dataGridViewCellStyle2 = new DataGridViewCellStyle();
        DataGridViewCellStyle dataGridViewCellStyle3 = new DataGridViewCellStyle();
        System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(Form1));
        txtServiceName = new TextBox();
        btnAdd = new Button();
        dgvServices = new DataGridView();
        colNome = new DataGridViewTextBoxColumn();
        colStatus = new DataGridViewTextBoxColumn();
        btnActivate = new Button();
        progressBar = new ProgressBar();
        lblStatus = new Label();
        groupBox1 = new GroupBox();
        groupBox2 = new GroupBox();
        btnRemover = new Button();
        panel1 = new Panel();
        ((System.ComponentModel.ISupportInitialize)dgvServices).BeginInit();
        groupBox1.SuspendLayout();
        groupBox2.SuspendLayout();
        panel1.SuspendLayout();
        SuspendLayout();
        // 
        // txtServiceName
        // 
        txtServiceName.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        txtServiceName.Font = new Font("Segoe UI", 11F);
        txtServiceName.Location = new Point(6, 28);
        txtServiceName.Name = "txtServiceName";
        txtServiceName.PlaceholderText = "Digite o nome do serviço...";
        txtServiceName.Size = new Size(445, 32);
        txtServiceName.TabIndex = 0;
        txtServiceName.KeyDown += TxtServiceName_KeyDown;
        // 
        // btnAdd
        // 
        btnAdd.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        btnAdd.Font = new Font("Segoe UI", 11F);
        btnAdd.Location = new Point(457, 26);
        btnAdd.Name = "btnAdd";
        btnAdd.Size = new Size(130, 36);
        btnAdd.TabIndex = 1;
        btnAdd.Text = "Adicionar";
        btnAdd.UseVisualStyleBackColor = true;
        btnAdd.Click += BtnAdd_Click;
        // 
        // dgvServices
        // 
        dgvServices.AllowUserToAddRows = false;
        dgvServices.AllowUserToDeleteRows = false;
        dgvServices.AllowUserToResizeRows = false;
        dataGridViewCellStyle1.BackColor = Color.FromArgb(240, 240, 240);
        dgvServices.AlternatingRowsDefaultCellStyle = dataGridViewCellStyle1;
        dgvServices.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
        dgvServices.BackgroundColor = SystemColors.Window;
        dgvServices.BorderStyle = BorderStyle.Fixed3D;
        dgvServices.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
        dgvServices.Columns.AddRange(new DataGridViewColumn[] { colNome, colStatus });
        dgvServices.Dock = DockStyle.Fill;
        dgvServices.Location = new Point(3, 23);
        dgvServices.MultiSelect = false;
        dgvServices.Name = "dgvServices";
        dgvServices.ReadOnly = true;
        dgvServices.RowHeadersVisible = false;
        dgvServices.RowHeadersWidth = 51;
        dataGridViewCellStyle2.Font = new Font("Segoe UI", 11F);
        dgvServices.RowsDefaultCellStyle = dataGridViewCellStyle2;
        dgvServices.RowTemplate.DefaultCellStyle.Font = new Font("Segoe UI", 11F);
        dgvServices.RowTemplate.Height = 35;
        dgvServices.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
        dgvServices.Size = new Size(589, 203);
        dgvServices.TabIndex = 2;
        // 
        // colNome
        // 
        colNome.AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill;
        colNome.HeaderText = "Nome do Serviço";
        colNome.MinimumWidth = 6;
        colNome.Name = "colNome";
        colNome.ReadOnly = true;
        // 
        // colStatus
        // 
        colStatus.AutoSizeMode = DataGridViewAutoSizeColumnMode.DisplayedCellsExceptHeader;
        colStatus.HeaderText = "Status";
        colStatus.MinimumWidth = 100;
        colStatus.Name = "colStatus";
        colStatus.ReadOnly = true;
        colStatus.Width = 100;
        // 
        // btnActivate
        // 
        btnActivate.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
        btnActivate.BackColor = Color.FromArgb(0, 120, 215);
        btnActivate.FlatStyle = FlatStyle.Flat;
        btnActivate.Font = new Font("Segoe UI", 12F, FontStyle.Bold);
        btnActivate.ForeColor = Color.White;
        btnActivate.Location = new Point(364, 17);
        btnActivate.Name = "btnActivate";
        btnActivate.Size = new Size(228, 52);
        btnActivate.TabIndex = 3;
        btnActivate.Text = "Ativar Serviços";
        btnActivate.UseVisualStyleBackColor = false;
        btnActivate.Click += BtnActivate_Click;
        // 
        // progressBar
        // 
        progressBar.Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
        progressBar.Location = new Point(12, 89);
        progressBar.Name = "progressBar";
        progressBar.Size = new Size(580, 23);
        progressBar.TabIndex = 4;
        progressBar.Visible = false;
        // 
        // lblStatus
        // 
        lblStatus.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
        lblStatus.AutoSize = true;
        lblStatus.Font = new Font("Segoe UI", 10F);
        lblStatus.ForeColor = Color.FromArgb(64, 64, 64);
        lblStatus.Location = new Point(12, 65);
        lblStatus.Name = "lblStatus";
        lblStatus.Size = new Size(135, 23);
        lblStatus.TabIndex = 5;
        lblStatus.Text = "Aguardando...";
        // 
        // groupBox1
        // 
        groupBox1.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
        groupBox1.Controls.Add(dgvServices);
        groupBox1.Font = new Font("Segoe UI", 10F, FontStyle.Bold);
        groupBox1.Location = new Point(12, 75);
        groupBox1.Name = "groupBox1";
        groupBox1.Size = new Size(595, 229);
        groupBox1.TabIndex = 6;
        groupBox1.TabStop = false;
        groupBox1.Text = "Serviços";
        // 
        // groupBox2
        // 
        groupBox2.Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
        groupBox2.Controls.Add(btnActivate);
        groupBox2.Controls.Add(progressBar);
        groupBox2.Controls.Add(lblStatus);
        groupBox2.Font = new Font("Segoe UI", 10F, FontStyle.Bold);
        groupBox2.Location = new Point(12, 310);
        groupBox2.Name = "groupBox2";
        groupBox2.Size = new Size(595, 125);
        groupBox2.TabIndex = 7;
        groupBox2.TabStop = false;
        groupBox2.Text = "Ação";
        // 
        // btnRemover
        // 
        btnRemover.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        btnRemover.Font = new Font("Segoe UI", 11F);
        btnRemover.Location = new Point(457, 68);
        btnRemover.Name = "btnRemover";
        btnRemover.Size = new Size(130, 36);
        btnRemover.TabIndex = 8;
        btnRemover.Text = "Remover";
        btnRemover.UseVisualStyleBackColor = true;
        btnRemover.Click += BtnRemover_Click;
        // 
        // panel1
        // 
        panel1.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        panel1.Controls.Add(txtServiceName);
        panel1.Controls.Add(btnRemover);
        panel1.Controls.Add(btnAdd);
        panel1.Location = new Point(12, 12);
        panel1.Name = "panel1";
        panel1.Size = new Size(595, 117);
        panel1.TabIndex = 9;
        // 
        // Form1
        // 
        AutoScaleDimensions = new SizeF(8F, 20F);
        AutoScaleMode = AutoScaleMode.Font;
        ClientSize = new Size(619, 447);
        Controls.Add(panel1);
        Controls.Add(groupBox2);
        Controls.Add(groupBox1);
        Icon = (Icon)resources.GetObject("$this.Icon");
        MinimumSize = new Size(637, 494);
        Name = "Form1";
        StartPosition = FormStartPosition.CenterScreen;
        Text = "Ativador de Serviços";
        ((System.ComponentModel.ISupportInitialize)dgvServices).EndInit();
        groupBox1.ResumeLayout(false);
        groupBox2.ResumeLayout(false);
        groupBox2.PerformLayout();
        panel1.ResumeLayout(false);
        panel1.PerformLayout();
        ResumeLayout(false);
    }

    private TextBox txtServiceName;
    private Button btnAdd;
    private DataGridView dgvServices;
    private Button btnActivate;
    private ProgressBar progressBar;
    private Label lblStatus;
    private GroupBox groupBox1;
    private GroupBox groupBox2;
    private Button btnRemover;
    private DataGridViewTextBoxColumn colNome;
    private DataGridViewTextBoxColumn colStatus;
    private Panel panel1;
}
